/* stream.c
 * - Core streaming functions/main loop.
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#include <shout/shout.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "config.h"
#include "input.h"
#include "im_playlist.h"
#include "net/resolver.h"
#include "signals.h"
#include "thread/thread.h"
#include "reencode.h"
#include "encode.h"
#include "inputmodule.h"
#include "stream_shared.h"
#include "stream.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#define MODULE "stream/"
#include "logging.h"

#define MAX_ERRORS 10

/* The main loop for each instance. Gets data passed to it from the stream
 * manager (which gets it from the input module), and streams it to the
 * specified server
 */
void *ices_instance_stream(void *arg)
{
	shout_conn_t conn;
	int ret;
	int errors=0;
	ref_buffer *buffer;
	stream_description *sdsc = arg;
	instance_t *stream = sdsc->stream;
	input_module_t *inmod = sdsc->input;
	queue_item *old;
	reencode_state *reenc=NULL;
	int reencoding = (inmod->type == ICES_INPUT_VORBIS) && stream->encode;
	int encoding = (inmod->type == ICES_INPUT_PCM) && stream->encode;
	encoder_state *enc=NULL;
	vorbis_comment vc;
	
	vorbis_comment_init(&vc);


	shout_init_connection(&conn);
	signal(SIGPIPE, signal_hup_handler);

	conn.ip = malloc(16);
	if(!resolver_getip(stream->hostname, conn.ip, 16))
	{
		LOG_ERROR1("Could not resolve hostname \"%s\"", stream->hostname);
		free(conn.ip);
		stream->died = 1;
		return NULL;
	}

	conn.port = stream->port;
	conn.password = strdup(stream->password);
	conn.mount = strdup(stream->mount);

	/* set the metadata for the stream */
	if (ices_config->stream_name)
		conn.name = strdup(ices_config->stream_name);
	if (ices_config->stream_genre)
		conn.genre = strdup(ices_config->stream_genre);
	if (ices_config->stream_description)
		conn.description = strdup(ices_config->stream_description);

	if(encoding)
	{
		if(inmod->metadata_update)
			inmod->metadata_update(inmod->internal, &vc);
		enc = encode_initialise(stream->channels, stream->samplerate,
				stream->bitrate, stream->serial++, &vc);
	}
	else if(reencoding)
		reenc = reencode_init(stream);

	if(shout_connect(&conn))
	{
		LOG_INFO3("Connected to server: %s:%d%s", 
				conn.ip, conn.port, conn.mount);

		while(1)
		{
			if(errors > MAX_ERRORS)
			{
				LOG_WARN0("Too many errors, shutting down");
				break;
			}

			buffer = stream_wait_for_data(stream);

			/* buffer being NULL means that either a fatal error occured,
			 * or we've been told to shut down
			 */
			if(!buffer)
				break;

			/* If data is NULL or length is 0, we should just skip this one.
			 * Probably, we've been signalled to shut down, and that'll be
			 * caught next iteration. Add to the error count just in case,
			 * so that we eventually break out anyway 
			 */
			if(!buffer->buf || !buffer->len)
			{
				LOG_WARN0("Bad buffer dequeued!");
				errors++;
				continue; 
			}

			if(encoding)
			{
				ogg_page og;
				int be = (inmod->subtype == INPUT_PCM_BE_16)?1:0;

				/* We use critical as a flag to say 'start a new stream' */
				if(buffer->critical)
				{
					encode_finish(enc);
					while(encode_flush(enc, &og) != 0)
					{
						ret = shout_send_data(&conn, og.header, og.header_len);
						ret = shout_send_data(&conn, og.body, og.body_len);
					}
					encode_clear(enc);


					if(inmod->metadata_update)
					{
						vorbis_comment_clear(&vc);
						vorbis_comment_init(&vc);

						inmod->metadata_update(inmod->internal, &vc);
					}

					enc = encode_initialise(stream->channels,stream->samplerate,
							stream->bitrate, stream->serial++, &vc);
				}

				encode_data(enc, (signed char *)(buffer->buf), buffer->len, be);

				while(encode_dataout(enc, &og) > 0)
				{
					/* FIXME: This is wrong. Get the return values right. */
					ret=shout_send_data(&conn, og.header, og.header_len);
					ret=shout_send_data(&conn, og.body, og.body_len);
				}
			}
			else if(reencoding)
			{
				unsigned char *buf;
				int buflen,ret2;

				ret2 = reencode_page(reenc, buffer, &buf, &buflen);
				if(ret2 > 0) 
				{
					ret = shout_send_data(&conn, buf, buflen);
					free(buf);
				}
				else if(ret2==0)
				{
					ret = -1; /* This way we don't enter the error handling 
								 code */
				}
				else
				{
					LOG_ERROR0("Fatal reencoding error encountered");
					errors = MAX_ERRORS+1;
					continue;
				}
			}
			else	
				ret = shout_send_data(&conn, buffer->buf, buffer->len);

			if(!ret)
			{
				LOG_ERROR1("Send error: %s", shout_strerror(&conn, conn.error));
				if(conn.error == SHOUTERR_SOCKET)
				{
					int i=0;

					/* While we're trying to reconnect, don't receive data
					 * to this instance, or we'll overflow once reconnect
					 * succeeds
					 */
					thread_mutex_lock(&ices_config->flush_lock);
					stream->skip = 1;

					/* Also, flush the current queue */
					input_flush_queue(stream->queue, 1);
					thread_mutex_unlock(&ices_config->flush_lock);
					
					while((i < stream->reconnect_attempts 
							|| i==-1) && !ices_config->shutdown)
					{
						i++;
						LOG_WARN0("Trying reconnect after server socket error");
						shout_disconnect(&conn);
						if(shout_connect(&conn))
						{
							LOG_INFO3("Connected to server: %s:%d%s", 
								conn.ip, conn.port, conn.mount);
							break;
						}
						else
						{
							LOG_ERROR3("Failed to reconnect to %s:%d (%s)",
								conn.ip,conn.port,
								shout_strerror(&conn,conn.error));
							if(i==stream->reconnect_attempts)
							{
								LOG_ERROR0("Reconnect failed too many times, "
										  "giving up.");
								errors = MAX_ERRORS+1; /* We want to die now */
							}
							else /* Don't try again too soon */
								sleep(stream->reconnect_delay); 
						}
					}
					stream->skip = 0;
				}
				errors++;
			}
			else
				errors=0;

			stream_release_buffer(buffer);

		}
	}
	else
	{
		LOG_ERROR3("Failed initial connect to %s:%d (%s)", 
				conn.ip,conn.port,shout_strerror(&conn,conn.error));
	}
	
	shout_disconnect(&conn);

	free(conn.ip);
	encode_clear(enc);
	reencode_clear(reenc);
	vorbis_comment_clear(&vc);

	stream->died = 1;
	return NULL;
}




