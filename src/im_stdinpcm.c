/* im_stdinpcm.c
 * - Raw PCM input from stdin
 *
 * $Id: im_stdinpcm.c,v 1.3 2002/08/03 15:05:38 msmith Exp $
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
#include <errno.h>
#include <ogg/ogg.h>

#include "thread.h"

#include "config.h"
#include "stream.h"

#include "inputmodule.h"

#include "im_stdinpcm.h"

#define MODULE "input-stdinpcm/"
#include "logging.h"

#define BUFSIZE 32768

static int event_handler(input_module_t *mod, enum event_type ev, void *param)
{
	switch(ev)
	{
		case EVENT_SHUTDOWN:
			if(mod)
			{
				if(mod->internal)
					free(mod->internal);
				free(mod);
			}
			break;
		case EVENT_NEXTTRACK:
			((stdinpcm_state *)mod->internal)->newtrack = 1;
			break;
		default:
			LOG_WARN1("Unhandled event %d", ev);
			return -1;
	}

	return 0;
}

/* Core streaming function for this module
 * This is what actually produces the data which gets streamed.
 *
 * returns:  >0  Number of bytes read
 *            0  Non-fatal error.
 *           <0  Fatal error.
 */
static int stdin_read(void *self, ref_buffer *rb)
{
	int result;
	stdinpcm_state *s = self;

	rb->buf = malloc(BUFSIZE);
    if(!rb->buf)
        return -1;

	result = fread(rb->buf, 1,BUFSIZE, stdin);

	rb->len = result;
	rb->aux_data = s->rate*s->channels*2;
	if(s->newtrack)
	{
		rb->critical = 1;
		s->newtrack = 0;
	}

	if(rb->len <= 0)
	{
		LOG_INFO0("Reached EOF, no more data available\n");
		free(rb->buf);
		return -1;
	}

	return rb->len;
}

input_module_t *stdin_open_module(module_param_t *params)
{
	input_module_t *mod = calloc(1, sizeof(input_module_t));
	stdinpcm_state *s;
	module_param_t *current;

	mod->type = ICES_INPUT_PCM;
	mod->getdata = stdin_read;
	mod->handle_event = event_handler;
	mod->metadata_update = NULL;

	mod->internal = malloc(sizeof(stdinpcm_state));
	s = mod->internal;

	s->rate = 44100; /* Defaults */
	s->channels = 2; 

	current = params;

	while(current)
	{
		if(!strcmp(current->name, "rate"))
			s->rate = atoi(current->value);
		else if(!strcmp(current->name, "channels"))
			s->channels = atoi(current->value);
		else
			LOG_WARN1("Unknown parameter %s for stdinpcm module", current->name);

		current = current->next;
	}

	return mod;
}


