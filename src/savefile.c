/* savefile.c
 * - Stream saving to file.
 *
 * $Id: savefile.c,v 1.3 2001/09/25 12:04:22 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 *
 * NOTE: Not currently actually used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "config.h"
#include "input.h"
#include "inputmodule.h"
#include "stream_shared.h"
#include "stream.h"

#define MODULE "stream-save/"
#include "logging.h"


void *savefile_stream(void *arg)
{
	stream_description *sdsc = arg;
	instance_t *stream = sdsc->stream;
	ref_buffer *buf;
	FILE *file;
	int ret;
	char *filename = stream->savefilename; 
	
	/* FIXME: Check for file existence, and append some unique string
	 * if it already exists.
	 */
	file = fopen(filename, "wb");

	if(!file)
	{
		LOG_ERROR1("Couldn't open file to save stream: %s", filename);
		stream->died = 1;
		return NULL;
	}

	LOG_INFO1("Saving stream to file: %s", filename);

	while(1)
	{
		buf = stream_wait_for_data(stream);

		if(!buf)
			break;

		if(!buf->buf || !buf->len)
		{
			LOG_WARN0("Bad buffer dequeue, not saving");
			continue;
		}

		ret = fwrite(buf->buf, 1, buf->len, file);

		if(ret != buf->len)
		{
			LOG_ERROR1("Error writing to file: %s", strerror(errno));
			/* FIXME: Try writing to a new file, or something */
			break;
		}

		stream_release_buffer(buf);
	}

	fclose(file);

	stream->died = 1;
	return NULL;
}
	
		
		


