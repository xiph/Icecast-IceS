/* im_oss.h
 * - read pcm data from oss devices
 *
 * $Id: im_oss.h,v 1.4 2003/03/28 01:07:37 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __IM_OSS_H__
#define __IM_OSS_H__

#include <thread/thread.h>
#include <ogg/ogg.h>
#include "inputmodule.h"

typedef struct
{
    int rate;
    int channels;

    int fd;
    char **metadata;
    int newtrack;
    mutex_t metadatalock;
} im_oss_state; 

input_module_t *oss_open_module(module_param_t *params);

#endif  /* __IM_OSS_H__ */
