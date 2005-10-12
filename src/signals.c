/* signals.c
 * - signal handling/setup
 *
 * $Id: signals.c,v 1.8 2003/03/28 01:07:37 karl Exp $
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
#include <signal.h>

#include <thread/thread.h>

#include "cfgparse.h"
#include "stream.h"
#include "input.h"
#include "inputmodule.h"
#include "event.h"

#define MODULE "signals/"
#include "logging.h"

extern volatile int metadata_update_signalled;

void signal_usr1_handler(int signum)
{
    LOG_INFO0("Metadata update requested");
    metadata_update_signalled = 1;
    thread_cond_broadcast(&ices_config->event_pending_cond);

    signal(SIGUSR1, signal_usr1_handler);
}

void signal_hup_handler(int signum)
{
    LOG_INFO0("Flushing logs");
    log_flush(ices_config->log_id);

    /* Now, let's tell it to move to the next track */
    ices_config->inmod->handle_event(ices_config->inmod,EVENT_NEXTTRACK,NULL);

    signal(SIGHUP, signal_hup_handler);
}

void signal_int_handler(int signum)
{
    /* Is a mutex needed here? Probably */
    if (!ices_config->shutdown) 
    {
        LOG_INFO0("Shutdown requested...");
        ices_config->shutdown = 1;
        thread_cond_broadcast(&ices_config->queue_cond);

        /* If user gives a second sigint, just die. */
        signal(SIGINT, SIG_DFL);
    }
}


void signals_setup(void)
{
    signal(SIGHUP, signal_hup_handler);
    signal(SIGINT, signal_int_handler);
    signal(SIGUSR1, signal_usr1_handler);
    signal(SIGPIPE, SIG_IGN);
}


