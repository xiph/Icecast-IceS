/* encode.c
 * - runtime encoding of PCM data.
 *
 * $Id: encode.c,v 1.7 2002/07/05 07:40:47 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include "config.h"
#include "encode.h"

#define MODULE "encode/"
#include "logging.h"

void encode_clear(encoder_state *s)
{
	if(s)
	{
	    LOG_DEBUG0("Clearing encoder engine");
		ogg_stream_clear(&s->os);
		vorbis_block_clear(&s->vb);
		vorbis_dsp_clear(&s->vd);
		vorbis_info_clear(&s->vi);
		free(s);
	}
}

encoder_state *encode_initialise(int channels, int rate, int managed,
        int min_br, int nom_br, int max_br, float quality,
		int serial, vorbis_comment *vc)
{
	encoder_state *s = calloc(1, sizeof(encoder_state));
	ogg_packet h1,h2,h3;

    if(managed)
        LOG_INFO5("Encoder initialising with bitrate management: %d channels, "
                 "%d Hz, minimum bitrate %d, nominal %d, maximum %d",
                 channels, rate, min_br, nom_br, max_br);
    else
	    LOG_INFO3("Encoder initialising at %d channels, %d Hz, quality %f", 
		    	channels, rate, quality);

	vorbis_info_init(&s->vi);

    if(managed)
	    vorbis_encode_init(&s->vi, channels, rate, max_br, nom_br, min_br);
    else
        vorbis_encode_init_vbr(&s->vi, channels, rate, quality*0.1);

	vorbis_analysis_init(&s->vd, &s->vi);
	vorbis_block_init(&s->vd, &s->vb);

	ogg_stream_init(&s->os, serial);

	vorbis_analysis_headerout(&s->vd, vc, &h1,&h2,&h3);
	ogg_stream_packetin(&s->os, &h1);
	ogg_stream_packetin(&s->os, &h2);
	ogg_stream_packetin(&s->os, &h3);

	s->in_header = 1;
    s->samplerate = rate;
    s->samples_in_current_page = 0;
    s->prevgranulepos = 0;

	return s;
}

void encode_data_float(encoder_state *s, float **pcm, int samples)
{
	float **buf;
	int i;

	buf = vorbis_analysis_buffer(&s->vd, samples); 

	for(i=0; i < s->vi.channels; i++)
	{
		memcpy(buf[i], pcm[i], samples*sizeof(float));
	}

	vorbis_analysis_wrote(&s->vd, samples);

    s->samples_in_current_page += samples;
}

/* Requires little endian data (currently) */
void encode_data(encoder_state *s, signed char *buf, int bytes, int bigendian)
{
	float **buffer;
	int i,j;
	int channels = s->vi.channels;
	int samples = bytes/(2*channels);

	buffer = vorbis_analysis_buffer(&s->vd, samples);

	if(bigendian)
	{
		for(i=0; i < samples; i++)
		{
			for(j=0; j < channels; j++)
			{
				buffer[j][i]=((buf[2*(i*channels + j)]<<8) |
						      (0x00ff&(int)buf[2*(i*channels + j)+1]))/32768.f;
			}
		}
	}
	else
	{
		for(i=0; i < samples; i++)
		{
			for(j=0; j < channels; j++)
			{
				buffer[j][i]=((buf[2*(i*channels + j) + 1]<<8) |
						      (0x00ff&(int)buf[2*(i*channels + j)]))/32768.f;
			}
		}
	}

	vorbis_analysis_wrote(&s->vd, samples);

    s->samples_in_current_page += samples;
}


/* Returns:
 *   0     No output at this time
 *   >0    Page produced
 *
 * Caller should loop over this to ensure that we don't end up with
 * excessive buffering in libvorbis.
 */
int encode_dataout(encoder_state *s, ogg_page *og)
{
	ogg_packet op;
	int result;

	if(s->in_header)
	{
		result = ogg_stream_flush(&s->os, og);
		if(result==0) 
		{
			s->in_header = 0;
			return encode_dataout(s,og);
		}
		else
			return 1;
	}
	else
	{
		while(vorbis_analysis_blockout(&s->vd, &s->vb)==1)
		{
			vorbis_analysis(&s->vb, NULL);
            vorbis_bitrate_addblock(&s->vb);

            while(vorbis_bitrate_flushpacket(&s->vd, &op)) 
    			ogg_stream_packetin(&s->os, &op);
		}

        /* FIXME: Make this threshold configurable.
         * We don't want to buffer too many samples in one page when doing
         * live encoding - that's fine for non-live encoding, but breaks
         * badly when doing things live. 
         * So, we flush the stream if we have too many samples buffered
         */
        if(s->samples_in_current_page > s->samplerate * 2)
        {
            LOG_DEBUG1("Forcing flush: Too many samples in current page (%d)", 
                    s->samples_in_current_page);
            result = ogg_stream_flush(&s->os, og);
        }
        else
		    result = ogg_stream_pageout(&s->os, og);

		if(result==0)
			return 0;
		else /* Page found! */
        {
            s->samples_in_current_page -= ogg_page_granulepos(og) - 
                    s->prevgranulepos;
            s->prevgranulepos = ogg_page_granulepos(og);
			return 1;
        }
	}
}

void encode_finish(encoder_state *s)
{
    int ret;
	ogg_packet op;
	vorbis_analysis_wrote(&s->vd, 0);

	while(vorbis_analysis_blockout(&s->vd, &s->vb)==1)
	{
        vorbis_analysis(&s->vb, NULL);
        vorbis_bitrate_addblock(&s->vb);
        while(vorbis_bitrate_flushpacket(&s->vd, &op))
		    ogg_stream_packetin(&s->os, &op);
	}

}

int encode_flush(encoder_state *s, ogg_page *og)
{
	int result = ogg_stream_pageout(&s->os, og);

	if(s->in_header)
	{
		LOG_ERROR0("Unhandled case: flushing stream before headers have been "
				  "output. Behaviour may be unpredictable.");
	}

	if(result<=0)
		return 0;
	else
		return 1;
}



