/* stream_shared.c
 * - Stream utility functions.
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
#include <unistd.h>

#include <thread/thread.h>
#include "config.h"
#include "input.h"
#include "inputmodule.h"
#include "stream_shared.h"
#include "stream.h"
#include "reencode.h"
#include "encode.h"

#define MODULE "stream-shared/"
#include "logging.h"

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
			ret = shout_send_data(&sdsc->conn, buf, buflen);
			free(buf);
            return ret;
		}
		else if(ret==0)
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
			encode_finish(sdsc->enc);
			while(encode_flush(sdsc->enc, &og) != 0)
			{
				ret = shout_send_data(&sdsc->conn, og.header, og.header_len);
				ret = shout_send_data(&sdsc->conn, og.body, og.body_len);
			}
			encode_clear(sdsc->enc);

			if(sdsc->input->metadata_update)
            {
				vorbis_comment_clear(&sdsc->vc);
				vorbis_comment_init(&sdsc->vc);

				sdsc->input->metadata_update(sdsc->input->internal, &sdsc->vc);
			}

			sdsc->enc = encode_initialise(sdsc->stream->channels,
                    sdsc->stream->samplerate, sdsc->stream->bitrate, 
                    sdsc->stream->serial++, &sdsc->vc);
		}

		encode_data(sdsc->enc, (signed char *)(buffer->buf), buffer->len, be);

		while(encode_dataout(sdsc->enc, &og) > 0)
		{
			ret = shout_send_data(&sdsc->conn, og.header, og.header_len);
			ret = shout_send_data(&sdsc->conn, og.body, og.body_len);
		}
                        
        return ret;
	}
	else	
		return shout_send_data(&sdsc->conn, buffer->buf, buffer->len);
}

