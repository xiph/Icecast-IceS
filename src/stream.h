/* stream.h
 * - Core streaming functions/main loop.
 *
 * $Id: stream.h,v 1.2 2001/09/25 12:04:22 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */


#ifndef __STREAM_H
#define __STREAM_H

#include <shout/shout.h>

#include "thread/thread.h"
#include "config.h"

typedef struct {
	unsigned char *buf;
	long len;
	int count;
	int critical;
	long aux_data;
} ref_buffer;

typedef struct _queue_item {
	ref_buffer *buf;
	struct _queue_item *next;
} queue_item;

typedef struct buffer_queue {
	queue_item *head, *tail;
	int length;
	mutex_t lock;
} buffer_queue;

void *ices_instance_stream(void *arg);
void *savefile_stream(void *arg);

#endif

