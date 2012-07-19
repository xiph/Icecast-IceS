/* cfgparse.c
 * - cfgparse file reading code, plus default settings.
 *
 * $Id: cfgparse.c,v 1.10 2004/03/11 17:16:08 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 * Copyright (c) 2006 Eric Faurot
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
#include <string.h>
#include <time.h>

/* these might need tweaking for other systems */
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <thread/thread.h>

#include "cfgparse.h"
#include "stream.h"

#define DEFAULT_BACKGROUND 0
#define DEFAULT_LOGPATH "/tmp"
#define DEFAULT_LOGFILE "ices.log"
#define DEFAULT_LOGLEVEL 1
#define DEFAULT_LOGSIZE 2048
#define DEFAULT_LOG_STDERR 1
#define DEFAULT_STREAM_NAME "unnamed ices stream"
#define DEFAULT_STREAM_GENRE "ices unset"
#define DEFAULT_STREAM_DESCRIPTION "no description set"
#define DEFAULT_PLAYLIST_MODULE "playlist"
#define DEFAULT_HOSTNAME "localhost"
#define DEFAULT_PORT 8000
#define DEFAULT_PASSWORD "password"
#define DEFAULT_USERNAME NULL
#define DEFAULT_MOUNT "/stream.ogg"
#define DEFAULT_MANAGED 0
#define DEFAULT_MIN_BITRATE -1
#define DEFAULT_NOM_BITRATE -1
#define DEFAULT_MAX_BITRATE -1
#define DEFAULT_QUALITY 3
#define DEFAULT_REENCODE 0
#define DEFAULT_DOWNMIX 0
#define DEFAULT_RESAMPLE 0
#define DEFAULT_RECONN_DELAY 2
#define DEFAULT_RECONN_ATTEMPTS 10
#define DEFAULT_RETRY_INIT 0
#define DEFAULT_MAXQUEUELENGTH 100 /* Make it _BIG_ by default */
#define DEFAULT_SAVEFILENAME NULL /* NULL == don't save */

/* helper macros so we don't have to write the same
** stupid code over and over
*/
#define SET_STRING(x) \
    do {\
        if (x) xmlFree(x);\
        (x) = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);\
        if (!(x)) (x) = xmlStrdup("");\
    } while (0) 

#define SET_INT(x) \
    do {\
        char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);\
        (x) = atoi(tmp);\
        if (tmp) xmlFree(tmp);\
    } while (0)

#define SET_FLOAT(x) \
    do {\
        char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);\
        (x) = atof(tmp);\
        if (tmp) xmlFree(tmp);\
    } while (0)

#define SET_PARM_STRING(p,x) \
        do {\
                if (x) xmlFree(x);\
                (x) = (char *)xmlGetProp(node, p);\
    } while (0)


/* this is the global config variable */
config_t *ices_config;

static int _using_default_instance = 1;

static void _free_instances(instance_t *instance)
{
    instance_t *next;
    
    next = NULL;
    do 
    {
        config_free_instance(instance);
        next = instance->next;
        free(instance);
        
        instance = next;
    } while (next != NULL);
}

void config_free_instance(instance_t *instance)
{
    if (instance->hostname) xmlFree(instance->hostname);
    if (instance->password) xmlFree(instance->password);
    if (instance->user) xmlFree(instance->user);
    if (instance->mount) xmlFree(instance->mount);
    if (instance->queue) 
    {
        thread_mutex_destroy(&instance->queue->lock);
        free(instance->queue);
    }
}

static void _set_instance_defaults(instance_t *instance)
{
    instance->hostname = xmlStrdup(DEFAULT_HOSTNAME);
    instance->port = DEFAULT_PORT;
    instance->password = xmlStrdup(DEFAULT_PASSWORD);
    instance->user = DEFAULT_USERNAME;
    instance->mount = xmlStrdup(DEFAULT_MOUNT);
    instance->managed = DEFAULT_MANAGED;
    instance->min_br = DEFAULT_MIN_BITRATE;
    instance->nom_br = DEFAULT_NOM_BITRATE;
    instance->max_br = DEFAULT_MAX_BITRATE;
    instance->quality = DEFAULT_QUALITY;
    instance->encode = DEFAULT_REENCODE;
    instance->downmix = DEFAULT_DOWNMIX;
    instance->resampleinrate = DEFAULT_RESAMPLE;
    instance->resampleoutrate = DEFAULT_RESAMPLE;
    instance->reconnect_delay = DEFAULT_RECONN_DELAY;
    instance->reconnect_attempts = DEFAULT_RECONN_ATTEMPTS;
    instance->retry_initial_connection = DEFAULT_RETRY_INIT;
    instance->max_queue_length = DEFAULT_MAXQUEUELENGTH;
    instance->savefilename = DEFAULT_SAVEFILENAME;

    instance->queue = calloc(1, sizeof(buffer_queue));
    thread_mutex_create(&instance->queue->lock);

    instance->next = NULL;
}

static void _parse_resample(instance_t *instance,xmlDocPtr doc, xmlNodePtr node)
{
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if(strcmp(node->name, "in-rate") == 0)
            SET_INT(instance->resampleinrate);
        else if(strcmp(node->name, "out-rate") == 0)
            SET_INT(instance->resampleoutrate);
    } while((node = node->next));
}

static void _parse_encode(instance_t *instance,xmlDocPtr doc, xmlNodePtr node)
{
    instance->encode = 1;
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "nominal-bitrate") == 0)
            SET_INT(instance->nom_br);
        else if (strcmp(node->name, "minimum-bitrate") == 0)
            SET_INT(instance->min_br);
        else if (strcmp(node->name, "maximum-bitrate") == 0)
            SET_INT(instance->max_br);
        else if (strcmp(node->name, "quality") == 0)
            SET_FLOAT(instance->quality);
        else if (strcmp(node->name, "samplerate") == 0)
            SET_INT(instance->samplerate);
        else if (strcmp(node->name, "channels") == 0)
            SET_INT(instance->channels);
        else if (strcmp(node->name, "managed") == 0)
            SET_INT(instance->managed);
        else if (strcmp(node->name, "flush-samples") == 0)
            SET_INT(instance->max_samples_ppage);
    } while ((node = node->next));
    if (instance->max_samples_ppage == 0)
        instance->max_samples_ppage = instance->samplerate;
    if (instance->max_samples_ppage < instance->samplerate/100)
        instance->max_samples_ppage = instance->samplerate/100;
}

static void _parse_metadata(instance_t *instance, config_t *config, 
        xmlDocPtr doc, xmlNodePtr node)
{
    do 
    {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "name") == 0) {
            if(instance)
                SET_STRING(instance->stream_name);
            else
                SET_STRING(config->stream_name);
        }
        else if (strcmp(node->name, "genre") == 0) {
            if(instance)
                SET_STRING(instance->stream_genre);
            else
                SET_STRING(config->stream_genre);
        }
        else if (strcmp(node->name, "description") == 0) {
            if(instance)
                SET_STRING(instance->stream_description);
            else
                SET_STRING(config->stream_description);
        }
	else if (strcmp(node->name, "url") == 0) {
	    if(instance)
		SET_STRING(instance->stream_url);
	    else
		SET_STRING(config->stream_url);
	}
    } while ((node = node->next));
}

static void _parse_instance(config_t *config, xmlDocPtr doc, xmlNodePtr node)
{
    instance_t *instance, *i;

    instance = (instance_t *)calloc(1, sizeof(instance_t));
    _set_instance_defaults(instance);

    do 
    {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "hostname") == 0)
            SET_STRING(instance->hostname);
        else if (strcmp(node->name, "port") == 0)
            SET_INT(instance->port);
        else if (strcmp(node->name, "password") == 0)
            SET_STRING(instance->password);
        else if (strcmp(node->name, "username") == 0)
            SET_STRING(instance->user);
        else if (strcmp(node->name, "yp") == 0)
            SET_INT(instance->public_stream);
        else if (strcmp(node->name, "savefile") == 0)
            SET_STRING(instance->savefilename);
        else if (strcmp(node->name, "mount") == 0)
            SET_STRING(instance->mount);
        else if(strcmp(node->name, "reconnectdelay") == 0)
            SET_INT(instance->reconnect_delay);
        else if(strcmp(node->name, "reconnectattempts") == 0)
            SET_INT(instance->reconnect_attempts);
        else if(strcmp(node->name, "retry-initial") == 0)
            SET_INT(instance->retry_initial_connection);
        else if(strcmp(node->name, "maxqueuelength") == 0)
            SET_INT(instance->max_queue_length);
        else if(strcmp(node->name, "downmix") == 0)
            SET_INT(instance->downmix);
        else if(strcmp(node->name, "resample") == 0)
            _parse_resample(instance, doc, node->xmlChildrenNode);
        else if (strcmp(node->name, "encode") == 0)
            _parse_encode(instance, doc, node->xmlChildrenNode);
        else if (strcmp(node->name, "metadata") == 0)
            _parse_metadata(instance, config, doc, node->xmlChildrenNode);
    } while ((node = node->next));

    instance->next = NULL;

    if (_using_default_instance) 
    {
        _using_default_instance = 0;
        _free_instances(config->instances);
        config->instances = NULL;
    }

    if (config->instances == NULL) 
    {
        config->instances = instance;
    } 
    else 
    {
        i = config->instances;
        while (i->next != NULL) i = i->next;
        i->next = instance;
    }
}

static void _parse_input(config_t *config, xmlDocPtr doc, xmlNodePtr node)
{
    module_param_t *param, *p;

    do 
    {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "module") == 0)
            SET_STRING(config->playlist_module);
        else if (strcmp(node->name, "param") == 0) {
            param = (module_param_t *)calloc(1, sizeof(module_param_t));
            SET_PARM_STRING("name", param->name);
            SET_STRING(param->value);
            param->next = NULL;

            if (config->module_params == NULL) 
            {
                config->module_params = param;
            } 
            else 
            {
                p = config->module_params;
                while (p->next != NULL) p = p->next;
                p->next = param;
            }
        }
    } while ((node = node->next));
}

static void _parse_stream(config_t *config, xmlDocPtr doc, xmlNodePtr node)
{
    do 
    {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "metadata") == 0)
            _parse_metadata(NULL, config, doc, node->xmlChildrenNode);
        else if (strcmp(node->name, "input") == 0)
            _parse_input(config, doc, node->xmlChildrenNode);
        else if (strcmp(node->name, "instance") == 0)
            _parse_instance(config, doc, node->xmlChildrenNode);
    } while ((node = node->next));
}

static void _parse_root(config_t *config, xmlDocPtr doc, xmlNodePtr node)
{
    do 
    {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;
        
        if (strcmp(node->name, "background") == 0)
            SET_INT(config->background);
        else if (strcmp(node->name, "logpath") == 0)
            SET_STRING(config->logpath);
        else if (strcmp(node->name, "logfile") == 0)
            SET_STRING(config->logfile);
        else if (strcmp(node->name, "loglevel") == 0)
            SET_INT(config->loglevel);
        else if (strcmp(node->name, "logsize") == 0)
            SET_INT(config->logsize);
        else if (strcmp(node->name, "consolelog") == 0)
            SET_INT(config->log_stderr);
        else if (strcmp(node->name, "pidfile") == 0)
            SET_STRING(config->pidfile);
        else if (strcmp(node->name, "stream") == 0)
            _parse_stream(config, doc, node->xmlChildrenNode);
    } while ((node = node->next));
}

static void _set_defaults(config_t *c)
{
    instance_t *instance;

    c->background = DEFAULT_BACKGROUND;
    c->logpath = xmlStrdup(DEFAULT_LOGPATH);
    c->logfile = xmlStrdup(DEFAULT_LOGFILE);
    c->logsize = DEFAULT_LOGSIZE;
    c->loglevel = DEFAULT_LOGLEVEL;
    c->log_stderr = DEFAULT_LOG_STDERR;

    c->stream_name = xmlStrdup(DEFAULT_STREAM_NAME);
    c->stream_genre = xmlStrdup(DEFAULT_STREAM_GENRE);
    c->stream_description = xmlStrdup(DEFAULT_STREAM_DESCRIPTION);
    c->stream_url = NULL;

    c->playlist_module = xmlStrdup(DEFAULT_PLAYLIST_MODULE);
    
    c->module_params = NULL;

    instance = (instance_t *)malloc(sizeof(instance_t));
    _set_instance_defaults(instance);
    c->instances = instance;
}

static void _free_params(module_param_t *param)
{
    module_param_t *next;
    next = NULL;
    do 
    {
        if (param->name) free(param->name);
        if (param->value) free(param->value);
        next = param->next;
        free(param);
        
        param = next;
    } while (next != NULL);
}

void config_initialize(void)
{
    ices_config = (config_t *)calloc(1, sizeof(config_t));
    xmlInitParser();
    _set_defaults(ices_config);
    srand(time(NULL));
}

void config_shutdown(void)
{
    if (ices_config == NULL) return;

    if (ices_config->module_params != NULL) 
    {
        _free_params(ices_config->module_params);
        ices_config->module_params = NULL;
    }

    if (ices_config->instances != NULL) 
    {
        _free_instances(ices_config->instances);
        ices_config->instances = NULL;
    }

    free(ices_config);
    ices_config = NULL;
    xmlCleanupParser();
}

int config_read(const char *fn)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    if (fn == NULL || strcmp(fn, "") == 0) return -1;

    doc = xmlParseFile(fn);
    if (doc == NULL) return -1;

    node = xmlDocGetRootElement(doc);
    if (node == NULL || strcmp(node->name, "ices") != 0) 
    {
        xmlFreeDoc(doc);
        return 0;
    }

    _parse_root(ices_config, doc, node->xmlChildrenNode);

    xmlFreeDoc(doc);

    return 1;
}

void config_dump(void)
{
    config_t *c = ices_config;
    module_param_t *param;
    instance_t *i;

    fprintf(stderr, "ices config dump:\n");
    fprintf(stderr, "background = %d\n", c->background);
    fprintf(stderr, "logpath = %s\n", c->logpath);
    fprintf(stderr, "logfile = %s\n", c->logfile);
    fprintf(stderr, "loglevel = %d\n", c->loglevel);
    fprintf(stderr, "\n");
    fprintf(stderr, "stream_name = %s\n", c->stream_name);
    fprintf(stderr, "stream_genre = %s\n", c->stream_genre);
    fprintf(stderr, "stream_description = %s\n", c->stream_description);
    fprintf(stderr, "stream_url = %s\n", c->stream_url ? c->stream_url : "");
    fprintf(stderr, "\n");
    fprintf(stderr, "playlist_module = %s\n", c->playlist_module);
    param = c->module_params;
    while(param)
    {
        fprintf(stderr, "module_param: %s = %s\n", param->name, param->value);
        param = param->next;
    }
    fprintf(stderr, "\ninstances:\n\n");

    i = c->instances;
    while (i) 
    {
        fprintf(stderr, "hostname = %s\n", i->hostname);
        fprintf(stderr, "port = %d\n", i->port);
        fprintf(stderr, "password = %s\n", i->password);
        fprintf(stderr, "mount = %s\n", i->mount);
        fprintf(stderr, "minimum bitrate = %d\n", i->min_br);
        fprintf(stderr, "nominal bitrate = %d\n", i->nom_br);
        fprintf(stderr, "maximum bitrate = %d\n", i->max_br);
        fprintf(stderr, "quality = %f\n", i->quality);
        fprintf(stderr, "managed = %d\n", i->managed);
        fprintf(stderr, "reencode = %d\n", i->encode);
        fprintf(stderr, "reconnect: %d times at %d second intervals\n", 
                i->reconnect_attempts, i->reconnect_delay);
        fprintf(stderr, "maxqueuelength = %d\n", i->max_queue_length);
        fprintf(stderr, "\n");

        i = i->next;
    }
    
    fprintf(stderr, "\n");
}






