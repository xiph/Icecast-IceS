/* input.c
 *  - Main producer control loop. Fetches data from input modules, and controls
 *    submission of these to the instance threads. Timing control happens here.
 *
 * $Id: input.c,v 1.17 2002/08/11 09:45:34 msmith Exp $
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
#include <sys/types.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <string.h>

#include "thread/thread.h"
#include "config.h"
#include "stream.h"
#include "timing.h"
#include "input.h"
#include "event.h"
#include "inputmodule.h"
#include "im_playlist.h"
#include "im_stdinpcm.h"

#ifdef HAVE_OSS
#include "im_oss.h"
#endif

#ifdef HAVE_SUN_AUDIO
#include "im_sun.h"
#endif

#ifdef _WIN32
typedef __int64 int64_t
typedef unsigned __int64 uint64_t
#endif

#define MODULE "input/"
#include "logging.h"

#define MAX_BUFFER_FAILURES 15

typedef struct _timing_control_tag 
{
	uint64_t starttime;
	uint64_t senttime;
	int samples;
	int oldsamples;
	int samplerate;
	long serialno;
} timing_control;

typedef struct _module 
{
	char *name;
	input_module_t *(*open)(module_param_t *params);
} module;

static module modules[] = {
	{ "playlist", playlist_open_module},
	{ "stdinpcm", stdin_open_module},
#ifdef HAVE_OSS
	{ "oss", oss_open_module},
#endif
#ifdef HAVE_SUN_AUDIO
	{ "sun", sun_open_module},
#endif
	{NULL,NULL}
};

/* This is identical to shout_sync(), really. */
static void _sleep(timing_control *control)
{
	int64_t sleep;

	/* no need to sleep if we haven't sent data */
	if (control->senttime == 0) return;

	sleep = ((double)control->senttime / 1000) - 
		(timing_get_time() - control->starttime);

	if(sleep > 0) timing_sleep((uint64_t)sleep);
}

static int _calculate_pcm_sleep(ref_buffer *buf, timing_control *control)
{
	control->senttime += ((double)buf->len * 1000000.)/((double)buf->aux_data);

    return 0;
}

static int _calculate_ogg_sleep(ref_buffer *buf, timing_control *control)
{
	/* Largely copied from shout_send(), without the sending happening.*/
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op;
	vorbis_info vi;
	vorbis_comment vc;
    int ret = 0;

	og.header_len = buf->aux_data;
	og.body_len = buf->len - buf->aux_data;
	og.header = buf->buf;
	og.body = buf->buf + og.header_len;

	if(control->serialno != ogg_page_serialno(&og)) {
		control->serialno = ogg_page_serialno(&og);

		control->oldsamples = 0;

		ogg_stream_init(&os, control->serialno);
		ogg_stream_pagein(&os, &og);
		ogg_stream_packetout(&os, &op);

		vorbis_info_init(&vi);
		vorbis_comment_init(&vc);

		if(vorbis_synthesis_headerin(&vi, &vc, &op) < 0) 
        {
            LOG_ERROR0("Timing control: can't determine sample rate for input, "
                       "not vorbis.");
            control->samplerate = 0;
            ret = -1;
        }
        else
		    control->samplerate = vi.rate;

		vorbis_comment_clear(&vc);
		vorbis_info_clear(&vi);
		ogg_stream_clear(&os);
	}

    if(ogg_page_granulepos(&og) == -1) {
        LOG_ERROR0("Timing control: corrupt timing information in vorbis file, cannot stream.");
        return -1;
    }

	control->samples = ogg_page_granulepos(&og) - control->oldsamples;
	control->oldsamples = ogg_page_granulepos(&og);

    if(control->samplerate) 
	    control->senttime += ((double)control->samples * 1000000 / 
		    	(double)control->samplerate);

    return ret;
}

void input_flush_queue(buffer_queue *queue, int keep_critical)
{
	queue_item *item, *next, *prev=NULL;

	LOG_DEBUG0("Input queue flush requested");

	thread_mutex_lock(&queue->lock);
	if(!queue->head)
	{
		thread_mutex_unlock(&queue->lock);
		return;
	}

	item = queue->head;
	while(item)
	{
		next = item->next;

		if(!(keep_critical && item->buf->critical))
		{
			thread_mutex_lock(&ices_config->refcount_lock);
			item->buf->count--;
			if(!item->buf->count)
			{
				free(item->buf->buf);
				free(item->buf);
			}
			thread_mutex_unlock(&ices_config->refcount_lock);

			if(prev)
				prev->next = next;
			else
				queue->head = next;

			free(item);
			item = next;

			queue->length--;
		}
		else
		{
			prev = item;
			item = next;
		}
	}

	/* Now, fix up the tail pointer */
	queue->tail = NULL;
	item = queue->head;

	while(item)
	{
		queue->tail = item;
		item = item->next;
	}

	thread_mutex_unlock(&queue->lock);
}

void input_loop(void)
{
	input_module_t *inmod=NULL;
	timing_control *control = calloc(1, sizeof(timing_control));
	instance_t *instance, *prev, *next;
	queue_item *queued;
	int shutdown = 0;
	int current_module = 0;
    int valid_stream = 1;
    int inc_count;
    int not_waiting_for_critical;

	while(ices_config->playlist_module && modules[current_module].open)
	{
		if(!strcmp(ices_config->playlist_module, modules[current_module].name))
		{
			inmod = modules[current_module].open(ices_config->module_params);
			break;
		}
		current_module++;
	}

	if(!inmod)
	{
		LOG_ERROR1("Couldn't initialise input module \"%s\"\n", 
				ices_config->playlist_module);
		return;
	}

	ices_config->inmod = inmod;

	thread_cond_create(&ices_config->queue_cond);
	thread_cond_create(&ices_config->event_pending_cond);
	thread_mutex_create(&ices_config->refcount_lock);
	thread_mutex_create(&ices_config->flush_lock);


	/* ok, basic config stuff done. Now, we want to start all our listening
	 * threads.
	 */

	instance = ices_config->instances;

	while(instance) 
	{
		stream_description *arg = calloc(1, sizeof(stream_description));
		arg->stream = instance;
		arg->input = inmod;
        /*
		if(instance->savefilename != NULL)
			thread_create("savefile", savefile_stream, arg, 1);
         */
		thread_create("stream", ices_instance_stream, arg, 1);

		instance = instance->next;
	}

	/* now we go into the main loop
	 * We shut down the main thread ONLY once all the instances
	 * have killed themselves.
	 */
	while(!shutdown) 
	{
		ref_buffer *chunk = calloc(1, sizeof(ref_buffer));
		buffer_queue *current;
		int ret;

		instance = ices_config->instances;
		prev = NULL;

		while(instance)
		{
			/* if an instance has died, get rid of it
			** this should be replaced with something that tries to 
			** restart the instance, probably.
			*/
			if (instance->died) 
			{
				LOG_DEBUG0("An instance died, removing it");
				next = instance->next;

				if (prev)
				{
					prev->next = next;
					prev = instance;
				}
				else
					ices_config->instances = next;

				/* Just in case, flush any existing buffers
				 * Locks shouldn't be needed, but lets be SURE */
				thread_mutex_lock(&ices_config->flush_lock);
				input_flush_queue(instance->queue, 0);
				thread_mutex_unlock(&ices_config->flush_lock);

				config_free_instance(instance);
				free(instance);

				instance = next;
				continue;
			}

			prev = instance;
			instance = instance->next;
		}

		instance = ices_config->instances;

		if(!instance)
		{
			shutdown = 1;
			free(chunk);
			continue;
		}

		if(ices_config->shutdown) /* We've been signalled to shut down, but */
		{						  /* the instances haven't done so yet... */
			timing_sleep(250); /* sleep for quarter of a second */
			free(chunk);
			continue;
		}
        
        /* If this is the first time through, set initial time. This should
         * be done before the call to inmod->getdata() below, in order to
         * properly keep time if this input module blocks.
         */
	    if(control->starttime == 0)
		    control->starttime = timing_get_time();

		/* get a chunk of data from the input module */
		ret = inmod->getdata(inmod->internal, chunk);

		/* input module signalled non-fatal error. Skip this chunk */
		if(ret==0)
		{
			free(chunk);
			continue;
		}

		/* Input module signalled fatal error, shut down - nothing we can do
		 * from here */
		if(ret < 0)
		{
			ices_config->shutdown = 1;
			thread_cond_broadcast(&ices_config->queue_cond);
			free(chunk);
			continue;
		}

        if(chunk->critical)
            valid_stream = 1;

		/* figure out how much time the data represents */
		switch(inmod->type)
		{
			case ICES_INPUT_VORBIS:
				ret = _calculate_ogg_sleep(chunk, control);
				break;
			case ICES_INPUT_PCM:
				ret = _calculate_pcm_sleep(chunk, control);
				break;
		}

        if(ret < 0) {
            /* Tell the input module to go to the next track, hopefully allowing
             * resync. */
	        ices_config->inmod->handle_event(ices_config->inmod,
                    EVENT_NEXTTRACK,NULL);
            valid_stream = 0;
        }

        inc_count = 0;
        not_waiting_for_critical = 0;

        if(valid_stream) 
        {
    		while(instance)
	    	{
                if(instance->wait_for_critical && !chunk->critical)
                {
                    instance = instance->next;
                    continue;

                }

                not_waiting_for_critical = 1;

                if(instance->skip)
	    		{
		    		instance = instance->next;
			    	continue;
    			}
    
	    		queued = malloc(sizeof(queue_item));
    
	    		queued->buf = chunk;
    			current = instance->queue;
    
                inc_count++;
    
	    		thread_mutex_lock(&current->lock);
    
	    		if(current->head == NULL)
		    	{
			    	current->head = current->tail = queued;
				    current->head->next = current->tail->next = NULL;
	    		}
    			else
			    {
		    		current->tail->next = queued;
				    queued->next = NULL;
    				current->tail = queued;
	    		}

		    	current->length++;
			    thread_mutex_unlock(&current->lock);

    			instance = instance->next;
	    	}
        }

        /* If everything is waiting for a critical buffer, force one
         * early. (This will take effect on the next pass through)
         */
        if(valid_stream && !not_waiting_for_critical) {
	        ices_config->inmod->handle_event(ices_config->inmod,
                    EVENT_NEXTTRACK,NULL);
		    instance = ices_config->instances;
            while(instance) {
			    thread_mutex_lock(&ices_config->flush_lock);
			    input_flush_queue(instance->queue, 0);
                instance->wait_for_critical = 0;
			    thread_mutex_unlock(&ices_config->flush_lock);
			    instance = instance->next;
            }
        }

		/* Make sure we don't end up with a 0-refcount buffer that 
		 * will never hit any of the free points. (this happens
		 * if all threads are set to skip, for example).
		 */
		thread_mutex_lock(&ices_config->refcount_lock);
        chunk->count += inc_count;
		if(!chunk->count)
		{
			free(chunk->buf);
			free(chunk);
		}
		thread_mutex_unlock(&ices_config->refcount_lock);

        if(valid_stream) {
    		/* wake up the instances */
	    	thread_cond_broadcast(&ices_config->queue_cond);

		    _sleep(control);
        }
	}

	LOG_DEBUG0("All instances removed, shutting down control thread.");

	thread_cond_destroy(&ices_config->queue_cond);
	thread_cond_destroy(&ices_config->event_pending_cond);
	thread_mutex_destroy(&ices_config->flush_lock);
	thread_mutex_destroy(&ices_config->refcount_lock);

	free(control);


	inmod->handle_event(inmod, EVENT_SHUTDOWN, NULL);

	return;
}


