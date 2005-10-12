/* im_stdinpcm.c
 * - Raw PCM input from stdin
 *
 * $Id: im_stdinpcm.c,v 1.10 2004/01/17 04:24:10 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ogg/ogg.h>

#include <thread/thread.h>

#include "cfgparse.h"
#include "stream.h"

#include "metadata.h"
#include "inputmodule.h"
#include "input.h"
#include "im_stdinpcm.h"

#define MODULE "input-stdinpcm/"
#include "logging.h"

#define BUFSIZE 32768

static void close_module(input_module_t *mod)
{
    if(mod)
    {
        if(mod->internal)
        {
            stdinpcm_state *s = mod->internal;
            thread_mutex_destroy(&s->metadatalock);
            free(s);
        }
        free(mod);
    }
}

static int event_handler(input_module_t *mod, enum event_type ev, void *param)
{
    stdinpcm_state *s = mod->internal;

    switch(ev)
    {
        case EVENT_SHUTDOWN:
	    close_module(mod);
            break;
        case EVENT_NEXTTRACK:
            ((stdinpcm_state *)mod->internal)->newtrack = 1;
            break;
        case EVENT_METADATAUPDATE:
            thread_mutex_lock(&s->metadatalock);
            if(s->metadata)
            {
                char **md = s->metadata;
                while(*md)
                    free(*md++);
                free(s->metadata);
            }

            s->metadata = (char **)param;
            s->newtrack = 1;
            thread_mutex_unlock(&s->metadatalock);
            break;
        default:
            LOG_WARN1("Unhandled event %d", ev);
            return -1;
    }

    return 0;
}

static void metadata_update(void *self, vorbis_comment *vc)
{
    stdinpcm_state *s = self;
    char **md;

    thread_mutex_lock(&s->metadatalock);

    md = s->metadata;

    if(md)
    {
        while(*md)
            vorbis_comment_add(vc, *md++);
    }

    thread_mutex_unlock(&s->metadatalock);
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
    input_calculate_pcm_sleep (rb->len, rb->aux_data);
    input_sleep ();

    return rb->len;
}

input_module_t *stdin_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    stdinpcm_state *s;
    module_param_t *current;
    int use_metadata = 1; /* Default to on */

    mod->type = ICES_INPUT_PCM;
    mod->getdata = stdin_read;
    mod->handle_event = event_handler;
    mod->metadata_update = metadata_update;

    mod->internal = malloc(sizeof(stdinpcm_state));
    s = mod->internal;

    s->rate = 44100; /* Defaults */
    s->channels = 2; 
    s->metadata = NULL;

    thread_mutex_create(&s->metadatalock);

    current = params;

    while(current)
    {
        if(!strcmp(current->name, "rate"))
            s->rate = atoi(current->value);
        else if(!strcmp(current->name, "channels"))
            s->channels = atoi(current->value);
        else if(!strcmp(current->name, "metadata"))
            use_metadata = atoi(current->value);
        else if(!strcmp(current->name, "metadatafilename"))
            ices_config->metadata_filename = current->value;
        else
            LOG_WARN1("Unknown parameter %s for stdinpcm module", current->name);

        current = current->next;
    }
    if(use_metadata)
    {
        if (ices_config->metadata_filename)
        {
            LOG_INFO0("Starting metadata update thread");
            thread_create("im_stdinpcm-metadata", metadata_thread_signal, mod, 1);
        }
    }

    return mod;
}

