/* ices.c
 * - Main startup, thread launching, and cleanup code.
 *
 * $Id: ices.c,v 1.19 2004/03/11 17:16:08 karl Exp $
 *
 * Copyright (c) 2001-2002 Michael Smith <msmith@xiph.org>
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
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <thread/thread.h>

#include "cfgparse.h"
#include "stream.h"
#include "signals.h"
#include "input.h"

#define MODULE "ices-core/"
#include "logging.h"

int main(int argc, char **argv)
{
    char logpath[FILENAME_MAX];
    int log;

    if (argc != 2) 
    {
        fprintf(stderr, PACKAGE_STRING "\n"
                "  (c) Copyright 2001-2004 The IceS Development Team <team@icecast.org>\n"
                "        Michael Smith <msmith@icecast.org>\n"
                "        Karl Heyes    <karl@xiph.org>\n"
                "        and others\n"
                "\n"
                "Usage: \"ices config.xml\"\n");
        return 1;
    }

    config_initialize();

    if (config_read(argv[1]) <= 0) 
    {
        fprintf(stderr, "Failed to read config file \"%s\"\n", argv[1]);
        goto fail;
    }
    if (ices_config->background)
    {
        int ret = 0;
        /* Start up new session, to lose old session and process group */
        switch (fork())
        {
        case 0: break; /* child continues */
        case -1: perror ("fork"); ret = -1;
        default:
            exit (ret);
        }

        /* Disassociate process group and controlling terminal */ 
        setsid();

        /* Become a NON-session leader so that a */
        /* control terminal can't be reacquired */
        switch (fork())
        {
        case 0: break; /* child continues */
        case -1: perror ("fork"); ret = -1;
        default:
            exit (ret);
        }
    }
    
    log_initialize();
    thread_initialize();
    shout_init();
    encode_init();
    signals_setup();

    snprintf(logpath, FILENAME_MAX, "%s/%s", ices_config->logpath, 
            ices_config->logfile);
    if(ices_config->log_stderr)
        log = log_open_file(stderr);
    else
    {
        log = log_open(logpath);
        if (log < 0)
            fprintf (stderr, "unable to open log %s\n", logpath);
        log_set_trigger (log, ices_config->logsize);
    }
    /* Set the log level, if requested - defaults to 2 (WARN) otherwise */
    if (ices_config->loglevel)
        log_set_level(log, ices_config->loglevel);

    ices_config->log_id = log;

    LOG_INFO0(PACKAGE_STRING " started...");
    if (ices_config->pidfile != NULL)
    {
        FILE *f = fopen (ices_config->pidfile, "w");
        if (f)
        {
            fprintf (f, "%i", getpid());
            fclose (f);
        }
        else
        {
            LOG_WARN1("pidfile \"%s\" cannot be written to", ices_config->pidfile);
            xmlFree (ices_config->pidfile);
            ices_config->pidfile = NULL;
        }
    }

    /* Start the core streaming loop */
    input_loop();

    if (ices_config->pidfile)
        remove (ices_config->pidfile);

    LOG_INFO0("Shutdown complete");

    log_close(log);

 fail:
    encode_close();
    shout_shutdown();
    config_shutdown();
    thread_shutdown();
    log_shutdown();

    return 0;
}


