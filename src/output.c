/* output.c
 * - Manage output instances
 *
 * $Id: output.c,v 1.2 2002/02/09 03:55:37 msmith Exp $
 *
 * Copyright (c) 2001-2002 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <thread/thread.h>
#include "config.h"
#include "input.h"
#include "stream.h"
#include "process.h"
#include "signals.h"

#define MODULE "output/"
#include "logging.h"

ref_buffer *instance_wait_for_data(instance_t *stream)
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

/* The main loop for each instance. Gets data passed to it from the stream
 *  * manager (which gets it from the input chain), and feeds it out through each
 *   * output chain.
 *    */
void *ices_instance_output(void *arg)
{
    int ret;
    instance_t *instance = arg;
    ref_buffer *in, *out;

    /* What is this for?? */
    signal(SIGPIPE, signal_hup_handler);

    while(1) {
        in = instance_wait_for_data(instance);

        if(!in)
            break;

        if(!in->buf || in->len <= 0) {
            LOG_WARN0("Bad buffer dequeued.");
            release_buffer(in);
            continue;
        }

        ret = process_chain(instance, instance->output_chain, in, &out);

        if(ret == -1) {
            LOG_DEBUG0("Non-fatal error  - chain not completed");
            continue;
        }
        else if(ret == -2) {
            LOG_ERROR0("Error received from output chain");
            break;
        }
    }

    /* Left main loop, we've shut down */
    instance->died = 1;

    return NULL;
}

