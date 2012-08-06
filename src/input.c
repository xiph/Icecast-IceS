/* input.c
 *  - Main producer control loop. Fetches data from input modules, and controls
 *    submission of these to the instance threads. Timing control happens here.
 *
 * $Id: input.c,v 1.32 2004/03/11 17:22:59 karl Exp $
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
#include <sys/types.h>
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#elif defined(HAVE_STDINT_H)
# include <stdint.h>
#endif
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <string.h>

#include <timing/timing.h>
#include <thread/thread.h>
#include "cfgparse.h"
#include "stream.h"
#include "input.h"
#include "event.h"
#include "signals.h"
#include "inputmodule.h"
#include "im_playlist.h"
#include "im_stdinpcm.h"

#ifdef HAVE_ROARAUDIO
#include "im_roar.h"
#endif

#ifdef HAVE_OSS
#include "im_oss.h"
#endif

#ifdef HAVE_SUN_AUDIO
#include "im_sun.h"
#endif

#ifdef HAVE_ALSA
#include "im_alsa.h"
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
    uint64_t samples;
    uint64_t oldsamples;
    unsigned samplerate;
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
#ifdef HAVE_ROARAUDIO
    { "roar", roar_open_module},
#endif
#ifdef HAVE_OSS
    { "oss", oss_open_module},
#endif
#ifdef HAVE_SUN_AUDIO
    { "sun", sun_open_module},
#endif
#ifdef HAVE_ALSA
    { "alsa", alsa_open_module},
#endif
    {NULL,NULL}
};

static timing_control control;


/* This is identical to shout_sync(), really. */
void input_sleep(void)
{
    int64_t sleep;

    /* no need to sleep if we haven't sent data */
    if (control.senttime == 0) return;

    sleep = ((double)control.senttime / 1000) - 
        (timing_get_time() - control.starttime);

    /* trap for long sleeps, typically indicating a clock change.  it's not */
    /* perfect though, as low bitrate/low samplerate vorbis can trigger this */
    if(sleep > 8000) {
        LOG_WARN1("Extended sleep requested (%ld ms), sleeping for 5 seconds",
                sleep);
        timing_sleep(5000);
    }
    else if(sleep > 0) 
        timing_sleep((uint64_t)sleep);
}

int input_calculate_pcm_sleep(unsigned bytes, unsigned bytes_per_sec)
{
    control.senttime += ((uint64_t)bytes * 1000000)/bytes_per_sec;

    return 0;
}

int input_calculate_ogg_sleep(ogg_page *page)
{
    static ogg_stream_state os;
    ogg_packet op;
    static vorbis_info vi;
    static vorbis_comment vc;
    static int need_start_pos, need_headers, state_in_use = 0;
    static int serialno = 0;
    static uint64_t offset;
    static uint64_t first_granulepos;

    if (ogg_page_granulepos(page) == -1)
    {
        LOG_ERROR0("Timing control: corrupt timing information in vorbis file, cannot stream.");
        return -1;
    }
    if (ogg_page_bos (page))
    {
        control.oldsamples = 0;

        if (state_in_use)
            ogg_stream_clear (&os);
        ogg_stream_init (&os, ogg_page_serialno (page));
        serialno = ogg_page_serialno (page);
        state_in_use = 1;
        vorbis_info_init (&vi);
        vorbis_comment_init (&vc);
        need_start_pos = 1;
        need_headers = 3;
        offset = (uint64_t)0;
    }
    if (need_start_pos)
    {
        int found_first_granulepos = 0;

        ogg_stream_pagein (&os, page);
        while (ogg_stream_packetout (&os, &op) == 1)
        {
            if (need_headers)
            {
                if (vorbis_synthesis_headerin (&vi, &vc, &op) < 0)
                {
                    LOG_ERROR0("Timing control: can't determine sample rate for input, not vorbis.");
                    control.samplerate = 0;
                    return -1;
                }
                need_headers--;
                control.samplerate = vi.rate;

                if (need_headers == 0)
                {
                    vorbis_comment_clear (&vc);
                    first_granulepos = (uint64_t)0;
                    return 0;
                }
                continue;
            }
            /* headers have been read */
            if (first_granulepos == 0 && op.granulepos > 0)
            {
                first_granulepos = op.granulepos;
                found_first_granulepos = 1;
            }
            offset += vorbis_packet_blocksize (&vi, &op) / 4;
        }
        if (!found_first_granulepos)
            return 0;

        need_start_pos = 0;
        control.oldsamples = first_granulepos - offset;
        vorbis_info_clear (&vi);
        ogg_stream_clear (&os);
        state_in_use = 0;
    }
    if (serialno != ogg_page_serialno (page))
    {
        LOG_ERROR0 ("Found page which does not belong to current logical stream");
        return -1;
    }
    control.samples = ogg_page_granulepos (page) - control.oldsamples;
    control.oldsamples = ogg_page_granulepos (page);

    control.senttime += ((uint64_t)control.samples * 1000000 /
            (uint64_t)control.samplerate);

    return 0;
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
    instance_t *instance, *prev, *next;
    queue_item *queued;
    int shutdown = 0;
    int current_module = 0;
    int valid_stream = 1;
    int inc_count;
    int not_waiting_for_critical;
    int foundmodule = 0;

    thread_cond_create(&ices_config->queue_cond);
    thread_cond_create(&ices_config->event_pending_cond);
    thread_mutex_create(&ices_config->refcount_lock);
    thread_mutex_create(&ices_config->flush_lock);

    memset (&control, 0, sizeof (control));

    while(ices_config->playlist_module && modules[current_module].open)
    {
        if(!strcmp(ices_config->playlist_module, modules[current_module].name))
        {
            foundmodule = 1;
            inmod = modules[current_module].open(ices_config->module_params);
            break;
        }
        current_module++;
    }

    if(!inmod)
    {
        if(foundmodule)
            LOG_ERROR1("Couldn't initialise input module \"%s\"", 
                    ices_config->playlist_module);
        else
            LOG_ERROR1("No input module named \"%s\" could be found", 
                    ices_config->playlist_module);
        return;
    }

    ices_config->inmod = inmod;


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
    /* treat as if a signal has arrived straight away */
    signal_usr1_handler (0);

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
                    prev->next = next;
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
        {                          /* the instances haven't done so yet... */
            timing_sleep(250); /* sleep for quarter of a second */
            free(chunk);
            continue;
        }
        
        /* If this is the first time through, set initial time. This should
         * be done before the call to inmod->getdata() below, in order to
         * properly keep time if this input module blocks.
         */
        if (control.starttime == 0)
            control.starttime = timing_get_time();

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

        }
    }

    LOG_INFO0 ("All instances removed, shutting down...");

    ices_config->shutdown = 1;
    thread_cond_broadcast(&ices_config->event_pending_cond);
    timing_sleep(250); /* sleep for quarter of a second */

    thread_cond_destroy(&ices_config->queue_cond);
    thread_cond_destroy(&ices_config->event_pending_cond);
    thread_mutex_destroy(&ices_config->flush_lock);
    thread_mutex_destroy(&ices_config->refcount_lock);

    inmod->handle_event(inmod, EVENT_SHUTDOWN, NULL);

    return;
}


