/* reencode.c
 * - runtime reencoding of vorbis audio (usually to lower bitrates).
 *
 * $Id: reencode.c,v 1.10 2003/12/22 14:01:09 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "reencode.h"
#include "cfgparse.h"
#include "stream.h"
#include "encode.h"
#include "audio.h"

#define MODULE "reencode/"
#include "logging.h"

reencode_state *reencode_init(instance_t *stream)
{
    reencode_state *new = calloc(1, sizeof(reencode_state));

    new->out_min_br = stream->min_br;
    new->out_nom_br = stream->nom_br;
    new->out_max_br = stream->max_br;
    new->quality = stream->quality;
    new->managed = stream->managed;

    new->out_samplerate = stream->samplerate;
    new->out_channels = stream->channels;
    new->current_serial = -1; /* FIXME: that's a valid serial */
    new->need_headers = 0;
    new->max_samples_ppage = stream->max_samples_ppage;

    return new;
}

void reencode_clear(reencode_state *s)
{
    if(s) 
    {
        LOG_DEBUG0("Clearing reencoder");
        ogg_stream_clear(&s->os);
        vorbis_block_clear(&s->vb);
        vorbis_dsp_clear(&s->vd);
        vorbis_comment_clear(&s->vc);
        vorbis_info_clear(&s->vi);

        free(s);
    }
}

/* Returns: -1 fatal death failure, argh!
 *              0 haven't produced any output yet
 *             >0 success
 */
int reencode_page(reencode_state *s, ref_buffer *buf,
        unsigned char **outbuf, int *outlen)
{
    ogg_page og, encog;
    ogg_packet op;
    int retbuflen=0, old;
    unsigned char *retbuf=NULL;
        

    og.header_len = buf->aux_data;
    og.body_len = buf->len - buf->aux_data;
    og.header = buf->buf;
    og.body = buf->buf + og.header_len;

    if(s->current_serial != ogg_page_serialno(&og))
    {
        s->current_serial = ogg_page_serialno(&og);

        if(s->encoder)
        {
            if(s->resamp) {
                resample_finish(s->resamp);
                encode_data_float(s->encoder, s->resamp->buffers, 
                        s->resamp->buffill);
            }
            encode_finish(s->encoder);
            while(encode_flush(s->encoder, &encog) != 0)
            {
                old = retbuflen;
                retbuflen += encog.header_len + encog.body_len;
                retbuf = realloc(retbuf, retbuflen);
                memcpy(retbuf+old, encog.header, encog.header_len);
                memcpy(retbuf+old+encog.header_len, encog.body, 
                        encog.body_len);
            }
        }
        encode_clear(s->encoder);
        s->encoder = NULL;
        resample_clear(s->resamp);
        s->resamp = NULL;
        downmix_clear(s->downmix);
        s->downmix = NULL;

        ogg_stream_clear(&s->os);
        ogg_stream_init(&s->os, s->current_serial);
        ogg_stream_pagein(&s->os, &og);

        vorbis_block_clear(&s->vb);
        vorbis_dsp_clear(&s->vd);
        vorbis_comment_clear(&s->vc);
        vorbis_info_clear(&s->vi);

        vorbis_info_init(&s->vi);
        vorbis_comment_init(&s->vc);

        if(ogg_stream_packetout(&s->os, &op) != 1)
        {
            LOG_ERROR0("Invalid primary header in stream");
            return -1;
        }

        if(vorbis_synthesis_headerin(&s->vi, &s->vc, &op) < 0)
        {
            LOG_ERROR0("Input stream not vorbis, can't reencode");
            return -1;
        }

        s->need_headers = 2; /* We still need two more header packets */
        LOG_DEBUG0("Reinitialising reencoder for new logical stream");
    }
    else
    {
        ogg_stream_pagein(&s->os, &og);
        while(ogg_stream_packetout(&s->os, &op) > 0)
        {
            if(s->need_headers)
            {
                vorbis_synthesis_headerin(&s->vi, &s->vc, &op);
                /* If this was the last header, init the rest */
                if(!--s->need_headers)
                {
                    vorbis_block_init(&s->vd, &s->vb);
                    vorbis_synthesis_init(&s->vd, &s->vi);
                    
                    s->encoder = encode_initialise(s->out_channels, 
                            s->out_samplerate, s->managed, 
                            s->out_min_br, s->out_nom_br, s->out_max_br,
                            s->quality, &s->vc);

                    if(!s->encoder) {
                        LOG_ERROR0("Failed to configure encoder for reencoding");
                        return -1;
                    }
                    s->encoder->max_samples_ppage = s->max_samples_ppage;
                    if(s->vi.rate != s->out_samplerate) {
                        s->resamp = resample_initialise(s->out_channels,
                                s->vi.rate, s->out_samplerate);
                    }
                    else
                        s->resamp = NULL;

                    if(s->vi.channels != s->out_channels) {
                        if(s->vi.channels == 2 && s->out_channels == 1)
                            s->downmix = downmix_initialise();
                        else {
                            LOG_ERROR2("Converting from %d to %d channels is not"
                                    " currently supported", s->vi.channels,
                                    s->out_channels);
                            return -1;
                        }
                    }
                    else
                        s->downmix = NULL;
                }
            }
            else
            {
                float **pcm;
                int samples;


                if(vorbis_synthesis(&s->vb, &op)==0)
                {
                    vorbis_synthesis_blockin(&s->vd, &s->vb);
                }

                while((samples = vorbis_synthesis_pcmout(&s->vd, &pcm))>0)
                {
                    if(s->downmix) {
                        downmix_buffer_float(s->downmix, pcm, samples);
                        if(s->resamp) {
                            resample_buffer_float(s->resamp, &s->downmix->buffer,
                                    samples);
                            encode_data_float(s->encoder, s->resamp->buffers,
                                    s->resamp->buffill);
                        }
                        else 
                            encode_data_float(s->encoder, &s->downmix->buffer,
                                    samples);
                    }
                    else if(s->resamp) {
                        resample_buffer_float(s->resamp, pcm, samples);
                        encode_data_float(s->encoder, s->resamp->buffers,
                                s->resamp->buffill);
                    }
                    else
                        encode_data_float(s->encoder, pcm, samples);
                    vorbis_synthesis_read(&s->vd, samples);
                }

                while(encode_dataout(s->encoder, &encog) != 0)
                {

                    old = retbuflen;
                    retbuflen += encog.header_len + encog.body_len;
                    retbuf = realloc(retbuf, retbuflen);
                    memcpy(retbuf+old, encog.header, encog.header_len);
                    memcpy(retbuf+old+encog.header_len, encog.body, 
                            encog.body_len);
                }
            }
        }
    }

    /* We've completed every packet from this page, so
     * now we can return what we wanted, depending on whether
     * we actually got data out or not
     */
    if(retbuflen > 0)
    {
        *outbuf = retbuf;
        *outlen = retbuflen;
        return retbuflen;
    }
    else
    {
        return 0;
    }
}


