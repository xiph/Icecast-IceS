/* im_stdinpcm.h
 * - stdin reading
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
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
	int newtrack;
} stdinpcm_state; 

input_module_t *stdin_open_module(module_param_t *params);

#endif  /* __IM_STDINPCM_H__ */
