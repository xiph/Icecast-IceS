/* stream_shared.c
 * - Stream utility functions.
 *
 * $Id: stream_shared.c,v 1.17 2003/12/22 14:01:09 karl Exp $
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
#include <unistd.h>

#include <thread/thread.h>
#include "cfgparse.h"
#include "input.h"
#include "inputmodule.h"
#include "stream_shared.h"
#include "stream.h"
#include "reencode.h"
#include "encode.h"
#include "audio.h"

#define MODULE "stream-shared/"
#include "logging.h"

int stream_send_data(stream_description *s, unsigned char *buf, 
        unsigned long len)
{
    int ret;

    if(s->stream->savefile)
    {
        int ret = fwrite(buf, 1, len, s->stream->savefile);
        if(ret != len) 
            LOG_ERROR1("Failed to write %d bytes to savefile", len);
    }

    ret = shout_send_raw(s->shout, buf, len);
    if(ret < 0)
        return 0; /* Force server-reconnect */
    else
        return ret;
}

void stream_release_buffer(ref_buffer *buf)
{
    thread_mutex_lock(&ices_config->refcount_lock);
    buf->count--;
    if(!buf->count)
    {
        free(buf->buf);
        free(buf);
    }
    thread_mutex_unlock(&ices_config->refcount_lock);
}

ref_buffer *stream_wait_for_data(instance_t *stream)
{
    ref_buffer *buffer;
    queue_item *old;

    thread_mutex_lock(&stream->queue->lock);
    while(!stream->queue->head && !ices_config->shutdown && !stream->kill)
    {
        thread_mutex_unlock(&stream->queue->lock);
        thread_cond_wait(&ices_config->queue_cond);
        thread_mutex_lock(&stream->queue->lock);
    }

    if(ices_config->shutdown || stream->kill)
    {
        LOG_DEBUG0("Shutdown signalled: thread shutting down");
        thread_mutex_unlock(&stream->queue->lock);
        return NULL;
    }

    buffer = stream->queue->head->buf;
    old = stream->queue->head;

    stream->queue->head = stream->queue->head->next;
    if(!stream->queue->head)
        stream->queue->tail = NULL;

    free(old);
    stream->queue->length--;
    thread_mutex_unlock(&stream->queue->lock);

    /* ok, we pulled something off the queue and the queue is
     * now empty - this means we're probably keeping up, so
     * clear one of the errors. This way, very occasional errors
     * don't cause eventual shutdown
     */
    if(!stream->queue->head && stream->buffer_failures>0)
        stream->buffer_failures--;

    return buffer;
}

/* Process a buffer (including reencoding or encoding, if desired).
 * Returns: >0 - success
 *           0 - shout error occurred
 *          -1 - no data produced
 *          -2 - fatal error occurred
 */
int process_and_send_buffer(stream_description *sdsc, ref_buffer *buffer)
{
    if(sdsc->reenc)
    {
        unsigned char *buf;
        int buflen,ret;

        ret = reencode_page(sdsc->reenc, buffer, &buf, &buflen);
        if(ret > 0) 
        {
            ret = stream_send_data(sdsc, buf, buflen);
            free(buf);
            return ret;
        }
        else if(ret==0) /* No data produced by reencode */
            return -1;
        else
        {
            LOG_ERROR0("Fatal reencoding error encountered");
            return -2;
        }
    }
    else if (sdsc->enc)
    {
        ogg_page og;
        int be = (sdsc->input->subtype == INPUT_PCM_BE_16)?1:0;
        int ret=1;

        /* We use critical as a flag to say 'start a new stream' */
        if(buffer->critical)
        {
            if(sdsc->resamp) {
                resample_finish(sdsc->resamp);
                encode_data_float(sdsc->enc, sdsc->resamp->buffers,
                        sdsc->resamp->buffill);
                resample_clear(sdsc->resamp);
                sdsc->resamp = resample_initialise (sdsc->stream->channels,
                        sdsc->stream->resampleinrate, sdsc->stream->resampleoutrate);
            }
            encode_finish(sdsc->enc);
            while(encode_flush(sdsc->enc, &og) != 0)
            {
                if ((ret = stream_send_data(sdsc, og.header, og.header_len)) == 0)
                    return 0;
                if ((ret = stream_send_data(sdsc, og.body, og.body_len)) == 0)
                    return 0;
            }
            encode_clear(sdsc->enc);

            if(sdsc->input->metadata_update)
            {
                vorbis_comment_clear(&sdsc->vc);
                vorbis_comment_init(&sdsc->vc);

                sdsc->input->metadata_update(sdsc->input->internal, &sdsc->vc);
            }

            sdsc->enc = encode_initialise(sdsc->stream->channels,
                    sdsc->stream->samplerate, sdsc->stream->managed, 
                    sdsc->stream->min_br, sdsc->stream->nom_br, 
                    sdsc->stream->max_br, sdsc->stream->quality,
                    &sdsc->vc);
            if(!sdsc->enc) {
                LOG_ERROR0("Failed to initialise encoder");
                return -2;
            }
            sdsc->enc->max_samples_ppage = sdsc->stream->max_samples_ppage;
        }

        if(sdsc->downmix) {
            downmix_buffer(sdsc->downmix, (signed char *)buffer->buf, buffer->len, be);
            if(sdsc->resamp) {
                resample_buffer_float(sdsc->resamp, &sdsc->downmix->buffer, 
                        buffer->len/4);
                encode_data_float(sdsc->enc, sdsc->resamp->buffers, 
                        sdsc->resamp->buffill);
            }
            else
                encode_data_float(sdsc->enc, &sdsc->downmix->buffer,
                       buffer->len/4);
        }
        else if(sdsc->resamp) {
            resample_buffer(sdsc->resamp, (signed char *)buffer->buf, 
                    buffer->len, be);
            encode_data_float(sdsc->enc, sdsc->resamp->buffers,
                    sdsc->resamp->buffill);
        }
        else {
            encode_data(sdsc->enc, (signed char *)(buffer->buf), 
                    buffer->len, be);
        }

        while(encode_dataout(sdsc->enc, &og) > 0)
        {
            if ((ret = stream_send_data(sdsc, og.header, og.header_len)) == 0)
                return 0;
            if ((ret = stream_send_data(sdsc, og.body, og.body_len)) == 0)
                return 0;
        }
                        
        return ret;
    }
    else    
        return stream_send_data(sdsc, buffer->buf, buffer->len);
}

