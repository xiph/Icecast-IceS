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






