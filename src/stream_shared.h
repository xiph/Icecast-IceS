/* stream.h
 * - Core streaming functions/main loop.
 *
 * $Id: stream_shared.h,v 1.4 2003/03/22 01:14:35 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __STREAM_SHARED_H
#define __STREAM_SHARED_H

#include "stream.h"
#include "cfgparse.h"
#include "input.h"

ref_buffer *stream_wait_for_data(instance_t *stream);
void stream_release_buffer(ref_buffer *buf);
int process_and_send_buffer(stream_description *sdsc, ref_buffer *buffer);

#endif


