/* stream.h
 * - Core streaming functions/main loop.
 *
 * $Id: stream_shared.h,v 1.3 2001/09/25 12:04:22 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __STREAM_SHARED_H
#define __STREAM_SHARED_H

#include "stream.h"
#include "config.h"
#include "input.h"

ref_buffer *stream_wait_for_data(instance_t *stream);
void stream_release_buffer(ref_buffer *buf);
int process_and_send_buffer(stream_description *sdsc, ref_buffer *buffer);

#endif


