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
	int ret;
	ref_buffer *buffer;
	stream_description *sdsc = arg;
	instance_t *stream = sdsc->stream;
	input_module_t *inmod = sdsc->input;
	int reencoding = (inmod->type == ICES_INPUT_VORBIS) && stream->encode;
	int encoding = (inmod->type == ICES_INPUT_PCM) && stream->encode;
	
	vorbis_comment_init(&sdsc->vc);

	shout_init_connection(&sdsc->conn);
	signal(SIGPIPE, signal_hup_handler);

	sdsc->conn.ip = malloc(16);
	if(!resolver_getip(stream->hostname, sdsc->conn.ip, 16))
	{
		LOG_ERROR1("Could not resolve hostname \"%s\"", stream->hostname);
		free(sdsc->conn.ip);
		stream->died = 1;
		return NULL;
	}

	sdsc->conn.port = stream->port;
	sdsc->conn.password = strdup(stream->password);
	sdsc->conn.mount = strdup(stream->mount);

	/* set the metadata for the stream */
	if (ices_config->stream_name)
		sdsc->conn.name = strdup(ices_config->stream_name);
	if (ices_config->stream_genre)
		sdsc->conn.genre = strdup(ices_config->stream_genre);
	if (ices_config->stream_description)
		sdsc->conn.description = strdup(ices_config->stream_description);

	if(encoding)
	{
		if(inmod->metadata_update)
			inmod->metadata_update(inmod->internal, &sdsc->vc);
		sdsc->enc = encode_initialise(stream->channels, stream->samplerate,
				stream->bitrate, stream->serial++, &sdsc->vc);
	}
	else if(reencoding)
		sdsc->reenc = reencode_init(stream);

    if(stream->savefilename != NULL) 
    {
        stream->savefile = fopen(stream->savefilename, "wb");
        if(!stream->savefile)
            LOG_ERROR2("Failed to open stream save file %s: %s", 
                    stream->savefilename, strerror(errno));
        else
            LOG_INFO1("Saving stream to file %s", stream->savefilename);
    }

	if(shout_connect(&sdsc->conn))
	{
		LOG_INFO3("Connected to server: %s:%d%s", 
				sdsc->conn.ip, sdsc->conn.port, sdsc->conn.mount);

		while(1)
		{
			if(stream->buffer_failures > MAX_ERRORS)
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
				stream->buffer_failures++;
				continue; 
			}

            ret = process_and_send_buffer(sdsc, buffer);

            /* No data produced */
            if(ret == -1)
                continue;
            /* Fatal error */
            else if(ret == -2)
            {
                stream->buffer_failures = MAX_ERRORS+1;
                continue;
            }
            /* Non-fatal shout error */
            else if(ret == 0)
			{
				LOG_ERROR1("Send error: %s", 
                        shout_strerror(&sdsc->conn, sdsc->conn.error));
				if(sdsc->conn.error == SHOUTERR_SOCKET)
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
					
					while((i < stream->reconnect_attempts ||
							stream->reconnect_attempts==-1) && 
                            !ices_config->shutdown)
					{
						i++;
						LOG_WARN0("Trying reconnect after server socket error");
						shout_disconnect(&sdsc->conn);
						if(shout_connect(&sdsc->conn))
						{
							LOG_INFO3("Connected to server: %s:%d%s", 
                                    sdsc->conn.ip, sdsc->conn.port, 
                                    sdsc->conn.mount);
							break;
						}
						else
						{
							LOG_ERROR3("Failed to reconnect to %s:%d (%s)",
								sdsc->conn.ip,sdsc->conn.port,
								shout_strerror(&sdsc->conn,sdsc->conn.error));
							if(i==stream->reconnect_attempts)
							{
								LOG_ERROR0("Reconnect failed too many times, "
										  "giving up.");
                                /* We want to die now */
								stream->buffer_failures = MAX_ERRORS+1; 
							}
							else /* Don't try again too soon */
								sleep(stream->reconnect_delay); 
						}
					}
					stream->skip = 0;
				}
				stream->buffer_failures++;
			}
			stream_release_buffer(buffer);
		}
	}
	else
	{
		LOG_ERROR3("Failed initial connect to %s:%d (%s)", 
				sdsc->conn.ip,sdsc->conn.port,
                shout_strerror(&sdsc->conn,sdsc->conn.error));
	}
	
	shout_disconnect(&sdsc->conn);

    if(stream->savefile != NULL) 
        fclose(stream->savefile);

	free(sdsc->conn.ip);
	encode_clear(sdsc->enc);
	reencode_clear(sdsc->reenc);
	vorbis_comment_clear(&sdsc->vc);

	stream->died = 1;
	return NULL;
}

