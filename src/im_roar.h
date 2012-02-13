/* im_oss.h
 * - Raw PCM/Ogg Vorbis input from RoarAudio
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 * Copyright (c) 2009 Philipp Schafft <lion@lion.leolix.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __IM_ROAR_H__
#define __IM_ROAR_H__

#include <thread/thread.h>
#include <roaraudio.h>
#include "inputmodule.h"

typedef struct
{
    struct roar_audio_info info;

    roar_vs_t * vss;

    char **metadata;
    int newtrack;
    mutex_t metadatalock;
} im_roar_state; 

input_module_t *roar_open_module(module_param_t *params);

#endif  /* __IM_ROAR_H__ */
