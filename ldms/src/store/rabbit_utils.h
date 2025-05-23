#ifndef ldms_librabbitmq_examples_utils_h
#define ldms_librabbitmq_examples_utils_h

/* This is the port for LDMS use of some key librabbitmq support functions.
 * Functions herein prefixed lrmq_ to avoid potential conflicts.
 * Calls to exit() have been removed in favor of logging and appropriate
 * return codes.
 */
/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MIT
 *
 * Portions created by Nichamon Naksinehaboon Copyright (c) 2023
 * Sandia National Laboratories and Open Grid Computing. All Rights Reserved.
 *
 * Portions created by Benjamin Allan are Copyright (c) 2015
 * Sandia National Laboratories and Open Grid Computing. All Rights Reserved.
 *
 * Portions created by Alan Antonuk are Copyright (c) 2012-2013
 * Alan Antonuk. All Rights Reserved.
 *
 * Portions created by VMware are Copyright (c) 2007-2012 VMware, Inc.
 * All Rights Reserved.
 *
 * Portions created by Tony Garnock-Jones are Copyright (c) 2009-2010
 * VMware, Inc. and Tony Garnock-Jones. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * ***** END LICENSE BLOCK *****
 *
 *
 */

#include <ldmsd.h>

/** Set the log subsystem to the caller log subsystem */
void rabbit_store_pi_log_set(ovis_log_t _pi_log);

/** print to stderr (but not "and exit") */
extern void lrmq_die(const char *fmt, ...);

/** print usingthe plugin log and return x. */
extern int lrmq_die_on_error(int x, char const *context);
/** print using the plugin log and return -1. */
extern int lrmq_die_on_amqp_error(amqp_rpc_reply_t x, char const *context);

/** print using the plugin log at info level. */
extern void lrmq_amqp_dump(void const *buffer, size_t len);

extern uint64_t lrmq_now_microseconds(void);
extern void lrmq_microsleep(int usec);

#endif
