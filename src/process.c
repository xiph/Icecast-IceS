/* process.c
 * - Processing chains - data sources, sinks, processing effects, reencoding,
 *   etc.
 *
 * $Id: process.c,v 1.3 2002/02/09 05:07:01 msmith Exp $
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
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "process.h"
#include "config.h"
#include "thread/thread.h"

#define MODULE "process/"
#include "logging.h"

#define DEBUG_BUFFERS


/* Return a newly allocate buffer, with refcount initialised to 1. */
ref_buffer *new_ref_buffer(media_type media, void *data, int len, int aux)
{
    ref_buffer *new = calloc(1, sizeof(ref_buffer) + (sizeof(int)*(aux-1)));
    new->type = media;
    new->buf = data;
    new->len = len;

    new->count = 1;

    return new;
}

void acquire_buffer(ref_buffer *buf)
{
    thread_mutex_lock(&ices_config->refcount_lock);

#ifdef DEBUG_BUFFERS
    if(!buf) {
        LOG_ERROR0("Null buffer aquired?");
	    thread_mutex_unlock(&ices_config->refcount_lock);
        return;
    }
    if(buf->count < 0)
        LOG_ERROR1("Error: refbuf has count %d before increment.", buf->count);
#endif

    buf->count++;
    
	thread_mutex_unlock(&ices_config->refcount_lock);
}

void release_buffer(ref_buffer *buf)
{
	thread_mutex_lock(&ices_config->refcount_lock);

#ifdef DEBUG_BUFFERS
    if(!buf) {
        LOG_ERROR0("Null buffer released?");
	    thread_mutex_unlock(&ices_config->refcount_lock);
        return;
    }
    if(buf->count <= 0)
        LOG_ERROR1("Error: refbuf has count %d before decrement.", buf->count);
#endif

	buf->count--;
    
	if(!buf->count)
	{
		free(buf->buf);
		free(buf);
	}
	thread_mutex_unlock(&ices_config->refcount_lock);
}

/* return values:
 * 0: normal return, success.
 * -1: chain terminated - insufficient data available?
 * -2: fatal error.
 */

int process_chain(instance_t *instance, process_chain_element *chain, 
        ref_buffer *in, ref_buffer **out) 
{
    int ret=0;

    while(chain) {
        if(chain->input_type != MEDIA_NONE && !in) {
            LOG_ERROR0("NULL input buffer where input required.");
            return -2;
        }
        else if(chain->input_type != MEDIA_NONE && chain->input_type != 
                MEDIA_DATA && in->type != chain->input_type) {
            LOG_ERROR2("Chain input does not match expected input! (%d != %d)",
                    in->type, chain->input_type);
            return -2;
        }
        
        ret = chain->process(instance, chain->priv_data, in, out);

        if(ret <= 0) {
            return ret;
        }

        if(chain->output_type != MEDIA_NONE && chain->output_type != MEDIA_DATA
                 && (*out)->type != chain->output_type) {
            LOG_ERROR0("Chain did not produce expected output type.");
            return -2;
        }

        in = *out;
        chain = chain->next;
    }

    return ret;
}

void create_event(process_chain_element *chain, event_type event, 
        void *param, int broadcast)
{
    /* chain->handle_event() returns 0 if it handles the event successfully.
     * We deliver to only one chain object unless the broadcast flag is set.
     */

    /* XXX: Try something like this?? Wake threads that don't do anything
     *      except when a flag gets set like this.
     * if(!chain) {
     *     thread_cond_broadcast(&ices_config->event_pending_cond);
     * } else { ...
     */
    while(chain) {
        if(!(chain->event_handler(chain, event, param) || broadcast)) 
            return;

        chain = chain->next;
    }

    if(!broadcast)
        LOG_INFO1("Non-broadcast event %d unhandled", event);
}


    
    
