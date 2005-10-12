/* logging.h
 * - macros used for logging. #define MODULE before including
 *
 * $Id: logging.h,v 1.5 2002/08/03 12:11:57 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __LOGGING_H
#define __LOGGING_H

#include "log/log.h"

#ifndef __GNUC__
#define __FUNCTION__ ""
#endif


#define LOG_ERROR0(x) log_write(ices_config->log_id,1, MODULE, __FUNCTION__,x)
#define LOG_ERROR1(x,a) log_write(ices_config->log_id,1, MODULE, __FUNCTION__,x, a)
#define LOG_ERROR2(x,a,b) log_write(ices_config->log_id,1, MODULE, __FUNCTION__,x, a,b)
#define LOG_ERROR3(x,a,b,c) log_write(ices_config->log_id,1, MODULE, __FUNCTION__,x, a,b,c)
#define LOG_ERROR4(x,a,b,c,d) log_write(ices_config->log_id,1, MODULE, __FUNCTION__,x, a,b,c,d)
#define LOG_ERROR5(x,a,b,c,d,e) log_write(ices_config->log_id,1, MODULE, __FUNCTION__,x, a,b,c,d,e)

#define LOG_WARN0(x) log_write(ices_config->log_id,2, MODULE, __FUNCTION__,x)
#define LOG_WARN1(x,a) log_write(ices_config->log_id,2, MODULE, __FUNCTION__,x, a)
#define LOG_WARN2(x,a,b) log_write(ices_config->log_id,2, MODULE, __FUNCTION__,x, a,b)
#define LOG_WARN3(x,a,b,c) log_write(ices_config->log_id,2, MODULE, __FUNCTION__,x, a,b,c)
#define LOG_WARN4(x,a,b,c,d) log_write(ices_config->log_id,2, MODULE, __FUNCTION__,x, a,b,c,d)

#define LOG_INFO0(x) log_write(ices_config->log_id,3, MODULE, __FUNCTION__,x)
#define LOG_INFO1(x,a) log_write(ices_config->log_id,3, MODULE, __FUNCTION__,x, a)
#define LOG_INFO2(x,a,b) log_write(ices_config->log_id,3, MODULE, __FUNCTION__,x, a,b)
#define LOG_INFO3(x,a,b,c) log_write(ices_config->log_id,3, MODULE, __FUNCTION__,x, a,b,c)
#define LOG_INFO4(x,a,b,c,d) log_write(ices_config->log_id,3, MODULE, __FUNCTION__,x, a,b,c,d)
#define LOG_INFO5(x,a,b,c,d,e) log_write(ices_config->log_id,3, MODULE, __FUNCTION__,x, a,b,c,d,e)

#define LOG_DEBUG0(x) log_write(ices_config->log_id,4, MODULE, __FUNCTION__,x)
#define LOG_DEBUG1(x,a) log_write(ices_config->log_id,4, MODULE, __FUNCTION__,x, a)
#define LOG_DEBUG2(x,a,b) log_write(ices_config->log_id,4, MODULE, __FUNCTION__,x, a,b)
#define LOG_DEBUG3(x,a,b,c) log_write(ices_config->log_id,4, MODULE, __FUNCTION__,x, a,b,c)
#define LOG_DEBUG4(x,a,b,c,d) log_write(ices_config->log_id,4, MODULE, __FUNCTION__,x, a,b,c,d)
#define LOG_DEBUG5(x,a,b,c,d,e) log_write(ices_config->log_id,4, MODULE, __FUNCTION__,x, a,b,c,d,e)


#endif /* __LOGGING_H */

