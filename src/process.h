/* process.h
 * - Processing chains
 *
 * $Id: process.h,v 1.2 2002/02/09 03:55:37 msmith Exp $
 *
 * Copyright (c) 2001-2002 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __PROCESS_H__
#define __PROCESS_H__

#include "event.h"

typedef enum {
    MEDIA_VORBIS,
    MEDIA_PCM,
    MEDIA_DATA,
    MEDIA_NONE,
} media_type;

typedef enum {
    SUBTYPE_PCM_BE_16,
    SUBTYPE_PCM_LE_16,
    SUBTYPE_PCM_FLOAT,
} media_subtype;

typedef enum {
    FLAG_CRITICAL = 1<<0,
    FLAG_BOS = 1<<1,

} buffer_flags;

typedef struct {
    media_type type;      /* Type of data held in buffer */
    media_subtype subtype;
    short channels;
    int rate;

	void *buf;            /* Actual data */
	int len;             /* Length of data (usually bytes, sometimes samples */

	short count;          /* Reference count */

	buffer_flags flags;   /* Flag: critical chunks must be processed fully */
    int aux_data_len;
	long aux_data[1];     /* Auxilliary data used for various purposes */
} ref_buffer;

/* Need some forward declarations */
struct _process_chain_element;
struct _instance_t;
struct _module_param_t;

/* Note that instance will be NULL for input chains */
typedef int (*process_func)(struct _instance_t *instance, void *data, 
        ref_buffer *in, ref_buffer **out);
typedef int (*event_func)(struct _process_chain_element *self, event_type event,
        void *param);
typedef int (*open_func)(struct _process_chain_element *self, 
        struct _module_param_t *params);

typedef struct _process_chain_element {
    char *name;
    open_func open;
    struct _module_param_t *params;
    
    process_func process;
    event_func   event_handler;
    void *priv_data;

    media_type input_type;
    media_type output_type;
   
    struct _process_chain_element *next;
} process_chain_element;

typedef struct _instance_t
{
	int buffer_failures;
	int died;
	int kill;
	int skip;
    int wait_for_critical;

	struct buffer_queue *queue;
    int max_queue_length;
    process_chain_element *output_chain;

    struct _instance_t *next;
} instance_t;

int process_chain(struct _instance_t *instance, process_chain_element *chain,
        ref_buffer *in, ref_buffer **out);

ref_buffer *new_ref_buffer(media_type media, void *data, int len, int aux);
void acquire_buffer(ref_buffer *buf);
void release_buffer(ref_buffer *buf);

void create_event(process_chain_element *chain, event_type event, void *param,
        int broadcast);

#endif /* __PROCESS_H__ */


