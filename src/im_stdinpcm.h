/* im_stdinpcm.h
 * - stdin reading
 *
 * $Id: im_stdinpcm.h,v 1.4 2003/07/06 06:20:34 brendan Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __IM_STDINPCM_H__
#define __IM_STDINPCM_H__

#include "inputmodule.h"
#include <ogg/ogg.h>

typedef struct
{
    int rate;
    int channels;
    char **metadata;
    int newtrack;
    mutex_t metadatalock;
} stdinpcm_state; 

input_module_t *stdin_open_module(module_param_t *params);

#endif  /* __IM_STDINPCM_H__ */
