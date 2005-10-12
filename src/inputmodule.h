/* inputmodule.h
 * - the interface for input modules to implement.
 *
 * $Id: inputmodule.h,v 1.3 2003/03/16 14:21:48 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __INPUTMODULE_H__
#define __INPUTMODULE_H__

#include "stream.h"
#include "event.h"

#include <vorbis/codec.h>

typedef enum _input_type {
    ICES_INPUT_PCM,
    ICES_INPUT_VORBIS,
    /* Can add others here in the future, if we want */
} input_type;

typedef enum _input_subtype {
    INPUT_PCM_LE_16,
    INPUT_PCM_BE_16,
} input_subtype;

typedef struct _input_module_tag {
    input_type type;
    input_subtype subtype;
    int (*getdata)(void *self, ref_buffer *rb);
    int (*handle_event)(struct _input_module_tag *self, enum event_type event, 
            void *param);
    void (*metadata_update)(void *self, vorbis_comment *vc);

    void *internal; /* For the modules internal state data */
} input_module_t;

#endif /* __INPUTMODULE_H__ */

