/* stream_rewrite.c
 * - Functions to rewrite a stream (at the ogg level) at runtime, to
 *   allow a) More reliable streaming of somewhat broken files, and
 *         b) Inserting comments into 'static' files at stream time.
 *
 * Heavily based on vcedit.c from vorbiscomment.
 *
 * $Id: stream_rewrite.c,v 1.3 2001/11/10 04:47:24 msmith Exp $
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

#include "config.h"
//#include "stream_rewrite.h"

typedef struct {
    ogg_sync_state    sync;
    ogg_stream_state  stream_out;
    ogg_stream_state  stream_in;

    vorbis_comment    vc;
    vorbis_info       vi;

    FILE             *in;
    long              serial;
    char             *vendor;
    int               prevW;

    short needflush,needout,in_header;

    ogg_int64_t granpos;

} stream_rewriter;

#define MODULE "stream-rewrite/"
#include "logging.h"

#define CHUNKSIZE 4096

/* Next two functions pulled straight from libvorbis, apart from one change
 * - we don't want to overwrite the vendor string.
 */
static void _v_writestring(oggpack_buffer *o,char *s, int len)
{
	while(len--)
	{
		oggpack_write(o,*s++,8);
	}
}

static int _commentheader_out(vorbis_comment *vc, char *vendor, ogg_packet *op)
{
	oggpack_buffer opb;

	oggpack_writeinit(&opb);

	/* preamble */  
	oggpack_write(&opb,0x03,8);
	_v_writestring(&opb,"vorbis", 6);

	/* vendor */
	oggpack_write(&opb,strlen(vendor),32);
	_v_writestring(&opb,vendor, strlen(vendor));

	/* comments */
	oggpack_write(&opb,vc->comments,32);
	if(vc->comments){
		int i;
		for(i=0;i<vc->comments;i++){
			if(vc->user_comments[i]){
				oggpack_write(&opb,vc->comment_lengths[i],32);
				_v_writestring(&opb,vc->user_comments[i], vc->comment_lengths[i]);
			}else{
				oggpack_write(&opb,0,32);
			}
		}
	}
	oggpack_write(&opb,1,1);

	op->packet = _ogg_malloc(oggpack_bytes(&opb));
	memcpy(op->packet, opb.buffer, oggpack_bytes(&opb));

	op->bytes=oggpack_bytes(&opb);
	op->b_o_s=0;
	op->e_o_s=0;
	op->granulepos=0;

	return 0;
}

static int _blocksize(stream_rewriter *s, ogg_packet *p)
{
	int this = vorbis_packet_blocksize(&s->vi, p);
	int ret = (this + s->prevW)/4;

	if(!s->prevW)
	{
		s->prevW = this;
		return 0;
	}

	s->prevW = this;
	return ret;
}

static int _fetch_next_packet(stream_rewriter *s, ogg_packet *p)
{
	int result;
	ogg_page og;
	char *buffer;
	int bytes;

	result = ogg_stream_packetout(&s->stream_in, p);

	if(result > 0)
		return 1;
	else
	{
		while(ogg_sync_pageout(&s->sync, &og) <= 0)
		{
			buffer = ogg_sync_buffer(&s->sync, CHUNKSIZE);
			bytes = fread(buffer,1, CHUNKSIZE, s->in);
			ogg_sync_wrote(&s->sync, bytes);
			if(bytes == 0) 
				return 0;
		}

		ogg_stream_pagein(&s->stream_in, &og);
		return _fetch_next_packet(s, p);
	}
}

static int _get_next_page(stream_rewriter *s, ogg_page *page)
{
    if(s->needflush)
    {
        if(ogg_stream_flush(&s->stream_out, page))
        {
            s->needflush=s->needout=0;
            return 1;
        }
    }
    else if(s->needout)
    {
        if(ogg_stream_pageout(&s->stream_out, page))
        {
            s->needflush=s->needout=0;
            return 1;
        }
    }

    return 0;
}

/* (what does this do?)
 * return value: <0  Error
 *                0  No more data in current file
 *               >0  Success, page returned.
 */
int stream_get_page(stream_rewriter *s, ogg_page *page)
{
    int res;
    ogg_packet packet;

    /* See if there's a pending page already - this should do the stuff with 
     * needflush/needout, remove that from below */
    if(_get_next_page(s, page) > 0)
        return 1;

    while((_fetch_next_packet(s, &packet) > 0))
    {
        /* Ok, deal with all the icky granulepos manipulations we need */
        int size = _blocksize(s, &packet);
        int flag=0;
        s->granpos += size;

        if(_get_next_page(s, page) > 0)
            flag = 1;

        if(packet.granulepos == -1)
            packet.granulepos = s->granpos;
        else
        {
            if(s->granpos > packet.granulepos)
            {
                s->granpos = packet.granulepos;
                s->needflush = 1;
            }
            else
                s->needout = 1;
        }

        /* Header packet. Note that this doesn't catch ALL header packets
         * (some dodgy files have it wrong, it catches them all in correct
         * streams), but it always gets the first packet (primary header)
         */
        if(packet.granulepos == 0)
        {
            s->in_header = 1;
            s->needflush = 1;

            ogg_stream_init(&s->stream_out, s->serial++);
            vorbis_info_init(&s->vi);
            vorbis_comment_init(&s->vc);
        }
        
        if(s->in_header) 
        {
            if(vorbis_synthesis_headerin(&s->vi, &s->vc, &packet) < 0)
            {
                LOG_ERROR0("Bad header in vorbis bitstream, cannot send");
                /* FIXME: Clear allocations first */
                return -1;
            }

            if(s->in_header++ == 3) /* We have all 3 header packets */
            {
                s->in_header = 0;
                s->needflush = 1;
            }

            /* FIXME: Allow comment header mangling here */
        }

        ogg_stream_packetin(&s->stream_out, &packet);

        if(flag == 1)
            return 1;
    }

    /* Fall out of above loop means EOS. Try flushing output stream */
    s->stream_out.e_o_s = 1;
    if(ogg_stream_flush(&s->stream_out, page))
        return 1;

    /* Final fallthrough. Nothing left. At all. 
     * FIXME: cope with chained streams properly. */
    return 0; 
}

