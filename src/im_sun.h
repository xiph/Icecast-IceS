/* im_sun.h
 * - read pcm data from sun devices
 *
 * $Id: im_sun.h,v 1.4 2003/07/01 23:53:06 karl Exp $
 *
 * by Ciaran Anscomb <ciarana@rd.bbc.co.uk>, based
 * on im_oss.c which is...
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __IM_SUN_H__
#define __IM_SUN_H__

#include <sys/audioio.h>
#include "inputmodule.h"
#include "thread/thread.h"
#include <ogg/ogg.h>

typedef struct
{
    audio_info_t device_info;
    int fd;
    int fdctl;
    char **metadata;
    int newtrack;
    mutex_t metadatalock;
} im_sun_state; 

input_module_t *sun_open_module(module_param_t *params);

#endif  /* __IM_SUN_H__ */

