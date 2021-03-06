/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2020 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <coll/rbt.h>
#include <pthread.h>
#include <ovis_util/util.h>
#include <json/json_util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_sampler.h"
#include "ldmsd_store.h"
#include "ldmsd_request.h"
#include "ldmsd_stream.h"
#include "ldms_xprt.h"
#include "ldmsd_notify.h"

/*
 * This file implements an LDMSD control protocol. The protocol is
 * message oriented and has message boundary markers.
 *
 * Every message has a unique msg_no identifier. Every record that is
 * part of the same message has the same msg_no value. The flags field
 * is a bit field as follows:
 *
 * 1 - Start of Message
 * 2 - End of Message
 *
 * The rec_len field is the size of the record including the header.
 * It is assumed that when reading from the socket that the next
 * message starts at cur_ptr + rec_len when cur_ptr starts at 0 and is
 * incremented by the read length for each socket operation.
 *
 * When processing protocol records, the header is stripped off and
 * all reqresp strings that share the same msg_no are concatenated
 * together until the record in which flags | End of Message is True
 * is received and then delivered to the ULP as a single message
 *
 */

int ldmsd_req_debug = 0; /* turn on / off using gdb or edit src to
                                 * see request/response debugging messages */

void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap);

__attribute__((format(printf, 1, 2)))
static inline
void __dlog(const char *fmt, ...)
{
	if (!ldmsd_req_debug)
		return;
	va_list ap;
	va_start(ap, fmt);
	__ldmsd_log(LDMSD_LALL, fmt, ap);
	va_end(ap);
}

static int msg_comparator(void *a, const void *b)
{
	ldmsd_msg_key_t ak = (ldmsd_msg_key_t)a;
	ldmsd_msg_key_t bk = (ldmsd_msg_key_t)b;
	int rc;

	rc = ak->conn_id - bk->conn_id;
	if (rc)
		return rc;
	return ak->msg_no - bk->msg_no;
}
struct rbt req_msg_tree = RBT_INITIALIZER(msg_comparator);
struct rbt rsp_msg_tree = RBT_INITIALIZER(msg_comparator);
pthread_mutex_t req_msg_tree_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rsp_msg_tree_lock = PTHREAD_MUTEX_INITIALIZER;

static
void ldmsd_req_ctxt_sec_get(ldmsd_req_ctxt_t rctxt, ldmsd_sec_ctxt_t sctxt)
{
	switch (rctxt->xprt->type) {
	case LDMSD_CFG_XPRT_CONFIG_FILE:
	case LDMSD_CFG_XPRT_CLI:
		ldmsd_sec_ctxt_get(sctxt);
		break;
	case LDMSD_CFG_XPRT_LDMS:
		ldms_xprt_cred_get(rctxt->xprt->xprt, NULL, &sctxt->crd);
		break;
	}
}

void __msg_key_get(ldmsd_cfg_xprt_t xprt, uint32_t msg_no,
						ldmsd_msg_key_t key_)
{
	key_->msg_no = msg_no;
	if (xprt->type == LDMSD_CFG_XPRT_LDMS) {
		/*
		 * Don't use the cfg_xprt directly because
		 * a new cfg_xprt get allocated
		 * every time LDMSD receives a record.
		 */
		key_->conn_id = (uint64_t)(unsigned long)xprt->ldms.ldms;
	} else {
		key_->conn_id = (uint64_t)(unsigned long)xprt;
	}
}

/* executable for all */
#define XALL 0111
/* executable for user, and group */
#define XUG 0110

typedef json_entity_t (*ldmsd_obj_handler_t)(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
struct request_handler_entry {
	const char *request;
	ldmsd_obj_handler_t handler;
	int flag; /* Lower 12 bit (mask 0777) for request permission.
		   * The rest is reserved for ldmsd_request use. */
};

static json_entity_t
ldmsd_cfgobj_create_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
ldmsd_cfgobj_update_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
ldmsd_cfgobj_delete_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
ldmsd_cfgobj_query_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
ldmsd_cfgobj_export_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
stream_subscribe_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
version_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
set_route_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);
static json_entity_t
test_protocol_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt);

static struct request_handler_entry request_handler_tbl[] = {
		{ "create",		ldmsd_cfgobj_create_handler,	XUG },
		{ "delete",		ldmsd_cfgobj_delete_handler,	XUG },
		{ "export",		ldmsd_cfgobj_export_handler,	XUG },
		{ "query",		ldmsd_cfgobj_query_handler,	XALL },
		{ "set_route",		set_route_handler,		XALL },
		{ "stream_subscribe",	stream_subscribe_handler,	XUG },
		{ "test_protocol",	test_protocol_handler,		XUG },
		{ "update",		ldmsd_cfgobj_update_handler,	XUG },
		{ "version",		version_handler,		XALL },
};

int request_handler_entry_cmp(const void *a, const void *b)
{
	struct request_handler_entry *a_, *b_;
	a_ = (struct request_handler_entry *)a;
	b_ = (struct request_handler_entry *)b;

	return strcmp(a_->request, b_->request);
}

struct cfgobj_type_handler_entry {
	ldmsd_cfgobj_create_fn_t create;
};

extern json_entity_t ldmsd_auth_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_env_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_listen_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_prdcr_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_updtr_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_strgp_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_smplr_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_setgrp_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
extern json_entity_t ldmsd_plugin_create(const char *name, short enabled,
					json_entity_t dft, json_entity_t spc,
					uid_t uid, gid_t gid);
static struct cfgobj_type_handler_entry cfgobj_type_handler_tbl[] = {
		[LDMSD_CFGOBJ_AUTH]	= { ldmsd_auth_create },
		[LDMSD_CFGOBJ_ENV]	= { ldmsd_env_create },
		[LDMSD_CFGOBJ_LISTEN]	= { ldmsd_listen_create },
		[LDMSD_CFGOBJ_PRDCR]	= { ldmsd_prdcr_create },
		[LDMSD_CFGOBJ_UPDTR]	= { ldmsd_updtr_create },
		[LDMSD_CFGOBJ_STRGP]	= { ldmsd_strgp_create },
		[LDMSD_CFGOBJ_SMPLR]	= { ldmsd_smplr_create },
		[LDMSD_CFGOBJ_PLUGIN]	= { ldmsd_plugin_create },
		[LDMSD_CFGOBJ_SETGRP]	= { ldmsd_setgrp_create },
};

static json_entity_t ldmsd_reply_new(const char *req_name, int msg_no, int status,
						const char *msg, json_entity_t result)
{
	json_entity_t reply;
	reply = json_dict_build(NULL,
			JSON_STRING_VALUE, "reply", req_name,
			JSON_INT_VALUE, "id", msg_no,
			JSON_INT_VALUE, "status", status,
			JSON_DICT_VALUE, "result", -2,
			-1);
	if (!reply)
		return NULL;
	if (msg) {
		reply = json_dict_build(reply,
				JSON_STRING_VALUE, "msg", msg,
				-1);
		if (!reply)
			return NULL;
	}

	if (result) {
		json_attr_mod(reply, "result", result);
	}
	return reply;
}

static int
ldmsd_reply_error_set(json_entity_t reply, int status, const char *msg)
{
	int rc;
	if (status >= 0) {
		if ((rc = json_attr_mod(reply, "status", status)))
			return rc;
	}

	if (msg) {
		if ((rc = json_attr_mod(reply, "msg", msg)))
			return rc;
	}
	return 0;
}

json_entity_t ldmsd_result_new(int errcode, const char *msg, json_entity_t value)
{
	json_entity_t result, a;
	result = json_dict_build(NULL, JSON_INT_VALUE, "status", errcode, -1);
	if (!result)
		goto oom;
	if (msg) {
		result = json_dict_build(result, JSON_STRING_VALUE, "msg", msg, -1);
		if (!result)
			goto oom;
	}
	if (value) {
		a = json_entity_new(JSON_ATTR_VALUE, "value", value);
		if (!a)
			goto oom;
		json_attr_add(result, a);
	}
	return result;
oom:
	return NULL;
}

static int ldmsd_reply_result_add(json_entity_t reply, const char *key, json_entity_t result)
{
	json_entity_t a;

	a = json_entity_new(JSON_ATTR_VALUE, key, result);
	if (!a)
		return ENOMEM;
	json_attr_add(json_value_find(reply, "result"), a);
	return 0;
}

int ldmsd_reply_send(ldmsd_req_ctxt_t reqc, json_entity_t reply)
{
	jbuf_t jb;
	int rc;

	if (LDMSD_CFG_XPRT_LDMS == reqc->xprt->type) {
		jb = json_entity_dump(NULL, reply);
		if (!jb) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return ENOMEM;
		}
		rc = ldmsd_append_response(reqc,
					LDMSD_REC_SOM_F | LDMSD_REC_EOM_F,
					jb->buf, jb->cursor);
	} else {
		rc = reqc->xprt->send_fn((void *)reqc->xprt, (char *)reply, 0);
	}
	return rc;
}

int ldmsd_request_send(ldms_t ldms, json_entity_t req_obj,
			ldmsd_req_resp_fn resp_fn, void *resp_args)
{
	int rc;
	jbuf_t jb = NULL;
	ldmsd_cfg_xprt_t xprt;
	ldmsd_req_ctxt_t reqc;
	struct ldmsd_msg_key key;

	xprt = ldmsd_cfg_xprt_ldms_new(ldms);
	if (!xprt) {
		rc = errno;
		goto err;
	}

	ldmsd_msg_key_get(xprt->ldms.ldms, &key);

	reqc = ldmsd_req_ctxt_alloc(&key, xprt);
	if (!reqc) {
		rc = errno;
		goto err;
	}

	reqc->resp_handler = resp_fn;
	reqc->resp_args = resp_args;

	json_attr_mod(req_obj, "id", key.msg_no);

	jb = json_entity_dump(NULL, req_obj);
	if (!jb) {
		rc = ENOMEM;
		goto err;
	}

	rc = ldmsd_append_request(reqc, LDMSD_REC_SOM_F | LDMSD_REC_EOM_F,
				  jb->buf, jb->cursor);
	jbuf_free(jb);
	if (rc)
		goto err;
	return rc;

err:
	if (xprt)
		ldmsd_cfg_xprt_ref_put(xprt, "create");
	if (jb)
		jbuf_free(jb);
	if (reqc)
		ldmsd_req_ctxt_ref_put(reqc, "create");
	return rc;
}

int __send_error(ldmsd_cfg_xprt_t xprt, struct ldmsd_msg_key *key,
				ldmsd_req_buf_t buf, uint32_t errcode,
				const char *errmsg_fmt, va_list errmsg_ap)
{
	int rc;
	char *str = NULL;
	size_t cnt;
	json_entity_t reply;
	va_list ap;
	jbuf_t jb;

	va_copy(ap, errmsg_ap);
	cnt = vsnprintf(str, 0, errmsg_fmt, ap);
	va_end(ap);
	str = malloc(cnt + 1);
	if (!str)
		return ENOMEM;
	va_copy(ap, errmsg_ap);
	cnt = vsnprintf(str, cnt + 1, errmsg_fmt, ap);
	va_end(ap);

	reply = ldmsd_reply_new("", key->msg_no, errcode, str, NULL);
	if (!reply) {
		free(str);
		return ENOMEM;
	}
	free(str);
	jb = json_entity_dump(NULL, reply);
	if (!jb) {
		json_entity_free(reply);
		return ENOMEM;
	}

	rc = ldmsd_append_msg_buffer(xprt, xprt->max_msg, key,
					(ldmsd_msg_send_fn_t)xprt->send_fn,
					buf, LDMSD_REC_SOM_F | LDMSD_REC_EOM_F,
					LDMSD_MSG_TYPE_RESP, jb->buf, jb->cursor);
	jbuf_free(jb);
	return rc;
}

/*
 * Any errors occur in any handler function must call \c ldmsd_send_error instead.
 *
 * if \c buf is NULL, the function will create its own buffer.
 *
 * Call the function only once to construct and send an error.
 */
int ldmsd_error_send(ldmsd_cfg_xprt_t xprt, uint32_t msg_no,
			ldmsd_req_buf_t _buf, uint32_t errcode,
			char *errmsg_fmt, ...)
{
	va_list errmsg_ap;
	ldmsd_req_buf_t buf;
	struct ldmsd_msg_key key;
	int rc = 0;

	__msg_key_get(xprt, msg_no, &key);
	if (_buf) {
		buf = _buf;
	} else {
		buf = ldmsd_req_buf_alloc(xprt->max_msg);
		if (!buf)
			return ENOMEM;
	}

	va_start(errmsg_ap, errmsg_fmt);
	rc = __send_error(xprt, &key, buf, errcode, errmsg_fmt, errmsg_ap);
	if (!_buf)
		ldmsd_req_buf_free(buf);
	va_end(errmsg_ap);
	return rc;
}

/*
 * The process request function takes records and collects
 * them into messages. These messages are then delivered to the req_id
 * specific handlers.
 *
 * The assumptions are the following:
 * 1. msg_no is unique on the socket
 * 2. There may be multiple messages outstanding on the same socket
 */
static ldmsd_req_ctxt_t find_req_ctxt(struct ldmsd_msg_key *key, int type)
{
	ldmsd_req_ctxt_t rm = NULL;
	struct rbt *tree;
	struct rbn *rbn;

	if (LDMSD_REQ_CTXT_RSP == type)
		tree = &rsp_msg_tree;
	else
		tree = &req_msg_tree;
	rbn = rbt_find(tree, key);
	if (rbn)
		rm = container_of(rbn, struct ldmsd_req_ctxt, rbn);
	return rm;
}

void ldmsd_req_ctxt_tree_lock(int type)
{
	if (LDMSD_REQ_CTXT_REQ == type)
		pthread_mutex_lock(&req_msg_tree_lock);
	else
		pthread_mutex_lock(&rsp_msg_tree_lock);
}

void ldmsd_req_ctxt_tree_unlock(int type)
{
	if (LDMSD_REQ_CTXT_REQ == type)
		pthread_mutex_unlock(&req_msg_tree_lock);
	else
		pthread_mutex_unlock(&rsp_msg_tree_lock);
}

/*
 * Caller must hold the tree lock
 */
ldmsd_req_ctxt_t ldmsd_req_ctxt_first(int type)
{
	ldmsd_req_ctxt_t reqc;
	struct rbn *rbn;
	struct rbt *tree = (type == LDMSD_REQ_CTXT_REQ)?&req_msg_tree:&rsp_msg_tree;
	rbn = rbt_min(tree);
	if (!rbn)
		return NULL;
	reqc = container_of(rbn, struct ldmsd_req_ctxt, rbn);
	return reqc;
}

/*
 * Caller must hold the tree lock
 */
ldmsd_req_ctxt_t ldmsd_req_ctxt_next(ldmsd_req_ctxt_t reqc)
{
	ldmsd_req_ctxt_t next;
	struct rbn *rbn;
	rbn = rbn_succ(&reqc->rbn);
	if (!rbn)
		return NULL;
	next = container_of(rbn, struct ldmsd_req_ctxt, rbn);
	return next;
}

/* The caller must _not_ hold the msg_tree lock. */
void __req_ctxt_del(ldmsd_req_ctxt_t reqc)
{
	ldmsd_req_ctxt_tree_lock(reqc->type);
	if (LDMSD_REQ_CTXT_REQ == reqc->type)
		rbt_del(&req_msg_tree, &reqc->rbn);
	else
		rbt_del(&rsp_msg_tree, &reqc->rbn);
	ldmsd_req_ctxt_tree_unlock(reqc->type);
	ldmsd_cfg_xprt_ref_put(reqc->xprt, "req_ctxt");
	if (reqc->recv_buf)
		ldmsd_req_buf_free(reqc->recv_buf);
	if (reqc->send_buf)
		ldmsd_req_buf_free(reqc->send_buf);
	free(reqc);
}

/*
 * max_msg_len must be a positive number.
 *
 * The caller must hold the msg_tree lock.
 */
ldmsd_req_ctxt_t
__req_ctxt_alloc(ldmsd_msg_key_t key, ldmsd_cfg_xprt_t xprt, int type)
{
	ldmsd_req_ctxt_t reqc;

	reqc = calloc(1, sizeof *reqc);
	if (!reqc)
		return NULL;

	reqc->recv_buf = ldmsd_req_buf_alloc(xprt->max_msg);
	if (!reqc->recv_buf)
		goto err;

	reqc->send_buf = ldmsd_req_buf_alloc(xprt->max_msg);
	if (!reqc->send_buf)
		goto err;

	ldmsd_cfg_xprt_ref_get(xprt, "req_ctxt");
	reqc->xprt = xprt;

	ref_init(&reqc->ref, "create", (ref_free_fn_t)__req_ctxt_del, reqc);
	reqc->key = *key;
	rbn_init(&reqc->rbn, &reqc->key);
	reqc->type = type;
	if (LDMSD_REQ_CTXT_RSP == type) {
		rbt_ins(&rsp_msg_tree, &reqc->rbn);
	} else {
		rbt_ins(&req_msg_tree, &reqc->rbn);
	}
	return reqc;
 err:
 	ldmsd_req_ctxt_ref_put(reqc, "create");
	return NULL;
}

/**
 * Allocate a request message context.
 */
ldmsd_req_ctxt_t
ldmsd_req_ctxt_alloc(struct ldmsd_msg_key *key, ldmsd_cfg_xprt_t xprt)
{
	return __req_ctxt_alloc(key, xprt, LDMSD_REQ_CTXT_RSP);
}

int ldmsd_append_response(ldmsd_req_ctxt_t reqc, int msg_flags,
				const char *data, size_t data_len)
{
	return ldmsd_append_msg_buffer(reqc->xprt, reqc->xprt->max_msg,
					&reqc->key,
					(ldmsd_msg_send_fn_t)reqc->xprt->send_fn,
					reqc->send_buf, msg_flags,
					LDMSD_MSG_TYPE_RESP, data, data_len);
}

int ldmsd_append_request(ldmsd_req_ctxt_t reqc, int msg_flags,
				const char *data, size_t data_len)
{
	return ldmsd_append_msg_buffer(reqc->xprt, reqc->xprt->max_msg,
					&reqc->key,
					(ldmsd_msg_send_fn_t)reqc->xprt->send_fn,
					reqc->send_buf, msg_flags,
					LDMSD_MSG_TYPE_REQ, data, data_len);
}

int
ldmsd_send_err_rec_adv(ldmsd_cfg_xprt_t xprt, uint32_t msg_no, uint32_t rec_len)
{
	struct ldmsd_msg_key key;
	json_entity_t reply = NULL;
	ldmsd_req_buf_t buf = NULL;
	jbuf_t jb = NULL;
	int rc;
	char msg[128];

	__msg_key_get(xprt, msg_no, &key);

	buf = ldmsd_req_buf_alloc(xprt->max_msg);
	if (!buf) {
		rc = ENOMEM;
		goto out;
	}

	snprintf(msg, 128, "The maximum length is '%" PRIu32, rec_len);

	reply = ldmsd_reply_new("rec_adv", msg_no, E2BIG, msg, NULL);
	if (!reply) {
		rc = ENOMEM;
		goto out;
	}

	jb = json_entity_dump(NULL, reply);
	if (!jb) {
		rc = ENOMEM;
		goto out;
	}

	rc = ldmsd_append_msg_buffer(xprt, xprt->max_msg, &key,
					(ldmsd_msg_send_fn_t)xprt->send_fn,
					buf, LDMSD_REC_EOM_F | LDMSD_REC_SOM_F,
					LDMSD_MSG_TYPE_RESP, jb->buf, jb->cursor);
out:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (reply)
		json_entity_free(reply);
	if (jb)
		jbuf_free(jb);
	return rc;
}

ldmsd_req_ctxt_t ldmsd_handle_record(ldmsd_rec_hdr_t rec, ldmsd_cfg_xprt_t xprt)
{
	ldmsd_req_ctxt_t reqc = NULL;
	char *oom_errstr = "ldmsd out of memory";
	int rc = 0;
	int req_ctxt_type = LDMSD_REQ_CTXT_REQ;
	struct ldmsd_msg_key key;
	size_t data_len = rec->rec_len - sizeof(*rec);

	if (LDMSD_MSG_TYPE_RESP == rec->type)
		req_ctxt_type = LDMSD_REQ_CTXT_RSP;
	else
		req_ctxt_type = LDMSD_REQ_CTXT_REQ;

	__msg_key_get(xprt, rec->msg_no, &key);
	ldmsd_req_ctxt_tree_lock(req_ctxt_type);

	reqc = find_req_ctxt(&key, req_ctxt_type);

	if (LDMSD_MSG_TYPE_RESP == rec->type) {
		/* Response messages */
		if (!reqc) {
			ldmsd_log(LDMSD_LERROR, "Cannot find the original request of "
					"a response number %d:%" PRIu64 "\n",
					key.msg_no, key.conn_id);
			rc = ldmsd_error_send(xprt, rec->msg_no, NULL, ENOENT,
				"Cannot find the original request of "
				"a message number %d.", rec->msg_no);
			if (rc == ENOMEM)
				goto oom;
			else
				goto err;
		}
	} else {
		/* request & stream messages */
		if (rec->flags & LDMSD_REC_SOM_F) {
			if (reqc) {
				rc = ldmsd_error_send(reqc->xprt,
						reqc->key.msg_no,
						reqc->send_buf,
						EEXIST,
						"Duplicate message number %d:"
						"%" PRIu64 "received",
						key.msg_no, key.conn_id);
				if (rc == ENOMEM)
					goto oom;
				else
					goto err;
			}
			reqc = __req_ctxt_alloc(&key, xprt, LDMSD_REQ_CTXT_REQ);
			if (!reqc)
				goto oom;
		} else {
			if (!reqc) {
				rc = ldmsd_error_send(xprt, rec->msg_no, NULL, ENOENT,
						"The message no %" PRIu32
						" was not found.", key.msg_no);
				ldmsd_log(LDMSD_LERROR, "The message no %" PRIu32 ":%" PRIu64
						" was not found.\n",
						key.msg_no, key.conn_id);
				goto err;
			}
		}
	}

	if (reqc->recv_buf->len - reqc->recv_buf->off < data_len) {
		reqc->recv_buf = ldmsd_req_buf_realloc(reqc->recv_buf,
					2 * (reqc->recv_buf->off + data_len));
		if (!reqc->recv_buf)
			goto oom;
	}
	memcpy(&reqc->recv_buf->buf[reqc->recv_buf->off], (char *)(rec + 1), data_len);
	reqc->recv_buf->off += data_len;

	ldmsd_req_ctxt_tree_unlock(req_ctxt_type);

	if (!(rec->flags & LDMSD_REC_EOM_F)) {
		/*
		 * LDMSD hasn't received the whole message.
		 */
		return NULL;
	}
	return reqc;

oom:
	rc = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "%s\n", oom_errstr);
err:
	errno = rc;
	ldmsd_req_ctxt_tree_unlock(req_ctxt_type);
	if (reqc)
		ldmsd_req_ctxt_ref_put(reqc, "create");
	return NULL;
}

json_entity_t __process_msg_requests(ldmsd_req_ctxt_t reqc,
					struct ldmsd_sec_ctxt *sctxt)
{
	json_entity_t req_type;
	int msg_no = reqc->key.msg_no;
	char *type_s;
	struct request_handler_entry *handler;
	json_entity_t reply = NULL;
	ldmsd_req_buf_t buf;
	int rc;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	req_type = json_value_find(reqc->json, "request");
	if (!req_type) {
		reply = ldmsd_reply_new("error", msg_no, EINVAL,
				"The 'request' attribute is missing.", NULL);
		goto out;
	}
	if (JSON_STRING_VALUE != json_entity_type(req_type)) {
		reply = ldmsd_reply_new("error", msg_no, EINVAL,
				"The 'request' attribute value is not "
				"a JSON string.", NULL);
		goto out;
	}
	type_s = json_value_str(req_type)->str;

	handler = bsearch(&type_s, request_handler_tbl,
			ARRAY_SIZE(request_handler_tbl),
			sizeof(*handler), request_handler_entry_cmp);
	if (!handler) {
		rc = ldmsd_req_buf_append(buf, "Request '%s' not supported.", type_s);
		if (rc < 0)
			goto oom;
		reply = ldmsd_reply_new(type_s, msg_no, ENOTSUP, buf->buf, NULL);
		goto out;
	}
	reply = handler->handler(reqc, sctxt);
	if (!reply)
		goto oom;
out:
	ldmsd_req_buf_free(buf);
	return reply;
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	if (buf)
		ldmsd_req_buf_free(buf);
	return NULL;
}

int ldmsd_process_msg_request(ldmsd_req_ctxt_t reqc)
{
	json_parser_t parser;
	int rc;
	json_entity_t reply;
	struct ldmsd_sec_ctxt sctxt;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	if (!reqc->json) {
		parser = json_parser_new(0);
		if (!parser) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return ENOMEM;
		}

		rc = json_parse_buffer(parser, reqc->recv_buf->buf,
				reqc->recv_buf->off, &reqc->json);
		json_parser_free(parser);
		if (rc) {
			ldmsd_log(LDMSD_LCRITICAL, "Failed to parse a JSON object string\n");
			ldmsd_error_send(reqc->xprt, reqc->key.msg_no, reqc->send_buf,
					rc, "Failed to parse a JSON object string");
			return rc;
		}
	}

	reply = __process_msg_requests(reqc, &sctxt);
	if (!reply) {
		if (errno == EINPROGRESS) {
			/*
			 * The reply is not ready. The handler will send the
			 * reply later when it is ready.
			 */
			rc = 0;
		} else {
			rc = errno;
		}
	} else {
		rc = ldmsd_reply_send(reqc, reply);
	}
	return rc;
}

int ldmsd_process_msg_response(ldmsd_req_ctxt_t reqc)
{
	int rc;
	json_parser_t parser;
	json_entity_t reply;
	struct ldmsd_sec_ctxt sctxt;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	parser = json_parser_new(0);
	if (!parser) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return ENOMEM;
	}

	rc = json_parse_buffer(parser, reqc->recv_buf->buf,
			reqc->recv_buf->off, &reqc->json);
	json_parser_free(parser);
	if (rc) {
		ldmsd_log(LDMSD_LCRITICAL, "Failed to parse a JSON object string\n");
		ldmsd_error_send(reqc->xprt, reqc->key.msg_no, reqc->send_buf, rc,
					"Failed to parse a JSON object string");
		return rc;
	}

	if (reqc->resp_handler) {
		rc = reqc->resp_handler(reqc, reqc->resp_args);
	} else {
		reply = __process_msg_requests(reqc, &sctxt);
		if (!reply) {
			if (errno == EINPROGRESS) {
				/*
				 * The reply is not ready. The handler will send the
				 * reply later when it is ready.
				 */
				rc = 0;
			} else {
				rc = errno;
			}
		} else {
			rc = ldmsd_reply_send(reqc, reply);
		}
	}

	return rc;
}

int ldmsd_process_msg_stream(ldmsd_req_ctxt_t reqc)
{
	size_t offset = 0;
	int rc = 0;
	char *stream_name, *data;
	enum ldmsd_stream_type_e stream_type;
	json_entity_t entity = NULL;
	json_parser_t p = NULL;


	__ldmsd_stream_extract_hdr(reqc->recv_buf->buf, &stream_name,
					&stream_type, &data, &offset);

	if (LDMSD_STREAM_JSON == stream_type) {
		p = json_parser_new(0);
		if (!p) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return ENOMEM;
		}
		rc = json_parse_buffer(p, data,
				reqc->recv_buf->off - offset, &entity);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "Failed to parse a JSON stream '%s'.\n",
					stream_name);
			goto out;
		}
	}

	ldmsd_stream_deliver(stream_name, stream_type, data,
					reqc->recv_buf->off - offset, entity);
out:
	if (p)
		json_parser_free(p);
	if (entity)
		json_entity_free(entity);
	/* Not sending any response back to the publisher */
	return rc;
}

int ldmsd_process_msg_notify(ldmsd_req_ctxt_t reqc)
{
	json_parser_t parser;
	int rc;
	struct ldmsd_sec_ctxt sctxt;
	json_entity_t jent;
	struct notify_handle_entry_s *tbl_ent;

	ldmsd_req_ctxt_sec_get(reqc, &sctxt);

	parser = json_parser_new(0);
	if (!parser) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return ENOMEM;
	}

	rc = json_parse_buffer( parser, reqc->recv_buf->buf,
				reqc->recv_buf->off, &reqc->json );
	json_parser_free(parser);
	if (rc) {
		ldmsd_log(LDMSD_LCRITICAL, "Failed to parse a JSON object string\n");
		ldmsd_error_send(reqc->xprt, reqc->key.msg_no, reqc->send_buf, rc,
				"Failed to parse a JSON object string");
		return rc;
	}

	jent = json_value_find(reqc->json, "type");
	if (!jent) {
		ldmsd_log(LDMSD_LWARNING, "`type` not found in the notify message.\n");
		return ENOENT;
	}
	if (jent->type != JSON_STRING_VALUE) {
		ldmsd_log(LDMSD_LWARNING, "notify message `type` is not a string.\n");
		return EINVAL;
	}
	tbl_ent = ldmsd_notify_handler_find(jent->value.str_->str);
	if (!tbl_ent) {
		ldmsd_log(LDMSD_LWARNING, "unknown type: %s\n", jent->value.str_->str);
		return ENOENT;
	}
	rc = ovis_access_check( sctxt.crd.uid, sctxt.crd.gid, 0111,
				geteuid(), getegid(), tbl_ent->flags );
	if (rc) {
		ldmsd_log(LDMSD_LWARNING, "`%s` notification handling "
				"rejected, permission denied\n",
				jent->value.str_->str);
		return rc;
	}
	rc = tbl_ent->handler(reqc);
	return rc;
}

static json_entity_t
ldmsd_cfgobj_create_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc, is_enabled;
	int msg_no = reqc->key.msg_no;
	json_entity_t schema, spec, dft, sp, v, reply, enabled, result;
	char *schema_s, *name_s;
	struct cfgobj_type_handler_entry handler;
	ldmsd_cfgobj_type_t type;
	ldmsd_req_buf_t buf;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf) {
		errno = ENOMEM;
		return NULL;
	}

	schema = json_value_find(reqc->json, "schema");
	schema_s = json_value_str(schema)->str;
	type = ldmsd_cfgobj_type_str2enum(schema_s);

	enabled = json_value_find(reqc->json, "enabled");
	if (enabled)
		is_enabled = json_value_bool(enabled);
	else
		is_enabled = 0;
	spec = json_value_find(reqc->json, "spec");
	if (!spec) {
		reply = ldmsd_reply_new("create", msg_no, EINVAL,
				"The 'spec' attribute is missing.", NULL);
		if (!reply)
			goto oom;
		goto out;
	}
	dft = json_value_find(reqc->json, "default");

	handler = cfgobj_type_handler_tbl[type];
	if (!handler.create) {
		snprintf(buf->buf, buf->len, "Schema '%s' not supported.", schema_s);
		reply = ldmsd_reply_new("create", msg_no, ENOTSUP, buf->buf, NULL);
		if (!reply)
			goto oom;
		return reply;
	}

	reply = ldmsd_reply_new("create", msg_no, 0, NULL, NULL);
	if (!reply)
		goto oom;
	for (sp = json_attr_first(spec); sp; sp = json_attr_next(sp)) {
		name_s = json_attr_name(sp)->str;
		v = json_attr_value(sp);
		result = handler.create(name_s, is_enabled, dft, v,
					sctxt->crd.uid, sctxt->crd.gid);
		if (!result)
			goto oom;
		rc = ldmsd_reply_result_add(reply, name_s, result);
		if (rc)
			goto oom;
	}
out:
	ldmsd_req_buf_free(buf);
	return reply;
oom:
	ldmsd_req_buf_free(buf);
	if (reply)
		json_entity_free(reply);
	errno = ENOMEM;
	return NULL;
}

static json_entity_t
ldmsd_cfgobj_update_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc;
	int msg_no = reqc->key.msg_no;
	short is_enabled = -1;
	ldmsd_cfgobj_t obj;
	json_entity_t schema, enabled, spec, sp, dft, v, re, reply = NULL, result;
	char *schema_s, *name_s, *regex_s;
	enum ldmsd_cfgobj_type cfgobj_type;
	regex_t regex;
	ldmsd_req_buf_t buf;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	schema = json_value_find(reqc->json, "schema");
	if (!schema) {
		reply = ldmsd_reply_new("update", msg_no, EINVAL,
				"'schema' is missing.", NULL);
		if (!reply)
			goto oom;
		return reply;
	}
	schema_s = json_value_str(schema)->str;
	enabled = json_value_find(reqc->json, "enabled");
	if (enabled)
		is_enabled = json_value_bool(enabled);
	dft = json_value_find(reqc->json, "default");
	spec = json_value_find(reqc->json, "spec");
	re = json_value_find(reqc->json, "re");

	cfgobj_type = ldmsd_cfgobj_type_str2enum(schema_s);
	if (cfgobj_type < 0) {
		snprintf(buf->buf, buf->len, "Schema '%s' not supported.", schema_s);
		reply = ldmsd_reply_new("update", msg_no, ENOTSUP, buf->buf, NULL);
		if (!reply)
			goto oom;
		return reply;
	}

	reply = ldmsd_reply_new("update", msg_no, 0, NULL, NULL);
	if (!reply)
		goto oom;

	if (!spec)
		goto re;

	for (sp = json_attr_first(spec); sp; sp = json_attr_next(sp)) {
		name_s = json_attr_name(sp)->str;
		v = json_attr_value(sp);

		obj = ldmsd_cfgobj_find(name_s, cfgobj_type);
		if (!obj) {
			rc = ldmsd_req_buf_append(buf, "cfgobj '%s' not found.", name_s);
			if (rc < 0)
				goto oom;
			result = ldmsd_result_new(ENOENT, buf->buf, NULL);
			if (!result)
				goto oom;
		} else {
			ldmsd_cfgobj_lock(obj);
			result = obj->update(obj, is_enabled, dft, v);
			ldmsd_cfgobj_unlock(obj);
			if (!result)
				goto oom;
			if (!ldmsd_is_initialized()) {
				/*
				 * The associated actions will be taken when
				 * all cfgobj requests are processed.
				 * No need to post an event.
				 */
			} else {
				if (is_enabled == 1) {
					ev_post(cfg, cfg, obj->enabled_ev, NULL);
				} else if (is_enabled == 0) {
					ev_post(cfg, cfg, obj->disabled_ev, NULL);
				} else {
					/* do nothing */
				}
			}
		}
		rc = ldmsd_reply_result_add(reply, name_s, result);
		if (rc)
			goto oom;
		ldmsd_req_buf_reset(buf);
	}

re:
	/* iterate through the re list */
	if (!re)
		goto out;
	for (v = json_item_first(re); v; v = json_item_next(v)) {
		regex_s = json_value_str(v)->str;
		memset(&regex, 0, sizeof(regex));
		rc = regcomp(&regex, regex_s, REG_EXTENDED | REG_NOSUB);
		if (rc) {
			size_t cnt;
			cnt = snprintf(buf->buf, buf->len, "Failed to compile regex '%s'.", regex_s);
			(void) regerror(rc, &regex, buf->buf, buf->len - cnt);
			rc = ldmsd_reply_error_set(reply, EINVAL, buf->buf);
			goto err;
		}

		obj = ldmsd_cfgobj_first_re(cfgobj_type, regex);
		while (obj) {
			ldmsd_cfgobj_lock(obj);
			result = obj->update(obj, is_enabled, dft, NULL);
			if (!result) {
				ldmsd_cfgobj_unlock(obj);
				goto oom;
			}
			rc = ldmsd_reply_result_add(reply, obj->name, result);
			if (rc) {
				ldmsd_cfgobj_unlock(obj);
				goto oom;
			}
			if (!ldmsd_is_initialized()) {
				/*
				 * The associated actions will be taken when
				 * all cfgobj requests are processed.
				 * No need to post an event.
				 */
			} else {
				if (is_enabled == 1) {
					ev_post(cfg, cfg, obj->enabled_ev, NULL);
				} else if (is_enabled == 0) {
					ev_post(cfg, cfg, obj->disabled_ev, NULL);
				} else {
					/* do nothing */
				}
			}
			ldmsd_cfgobj_unlock(obj);
			obj = ldmsd_cfgobj_next_re(obj, regex);
		}
	}
out:
	ldmsd_req_buf_free(buf);
	return reply;
oom:
	rc = ENOMEM;
err:
	errno = rc;
	if (buf)
		ldmsd_req_buf_free(buf);
	if (reply)
		json_entity_free(reply);
	return NULL;
}

static json_entity_t
ldmsd_cfgobj_delete_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc;
	int msg_no = reqc->key.msg_no;
	ldmsd_cfgobj_t obj;
	json_entity_t schema, key, item, re, reply = NULL, result;
	char *schema_s, *name_s, *regex_s;
	enum ldmsd_cfgobj_type cfgobj_type;
	ldmsd_req_buf_t buf;
	regex_t regex;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	schema = json_value_find(reqc->json, "schema");
	schema_s = json_value_str(schema)->str;
	key = json_value_find(reqc->json, "key");
	re = json_value_find(reqc->json, "re");

	cfgobj_type = ldmsd_cfgobj_type_str2enum(schema_s);
	if (cfgobj_type < 0) {
		rc = ldmsd_req_buf_append(buf, "schema '%s' not supported.", schema_s);
		if (rc < 0)
			goto oom;
		reply = ldmsd_reply_new("delete", msg_no, ENOTSUP, buf->buf, NULL);
		if (!reply)
			goto oom;
		return reply;
	}

	reply = ldmsd_reply_new("delete", msg_no, 0, NULL, NULL);
	if (!reply)
		goto oom;
	/* Iterate through the name list */
	if (!key)
		goto re;
	for (item = json_item_first(key); item; item = json_item_next(item)) {
		name_s = json_value_str(item)->str;
		obj = ldmsd_cfgobj_find(name_s, cfgobj_type);
		if (!obj) {
			result = ldmsd_result_new(ENOENT, NULL, NULL);
			if (!result)
				goto oom;
		} else {
			ldmsd_cfg_lock(cfgobj_type);
			result = obj->delete(obj);
			ldmsd_cfg_unlock(cfgobj_type);
		}
		ldmsd_cfgobj_put(obj); /* Put the find reference */
		rc = ldmsd_reply_result_add(reply, name_s, result);
		if (rc)
			goto oom;
	}

	/* iterate through the re list */
re:
	if (!re)
		goto out;
	for (item = json_item_first(re); item; item = json_item_next(item)) {
		regex_s = json_value_str(item)->str;
		memset(&regex, 0, sizeof(regex));
		rc = regcomp(&regex, regex_s, REG_EXTENDED | REG_NOSUB);
		if (rc) {
			rc = ldmsd_req_buf_append(buf, "Failed to compile regex '%s'.", regex_s);
			if (rc < 0)
				goto oom;
			(void) regerror(rc, &regex, &buf->buf[buf->off],
							buf->len - buf->off);
			rc = ldmsd_reply_error_set(reply, rc, buf->buf);
			if (rc)
				goto err;
		}

		ldmsd_cfg_lock(cfgobj_type);
		obj = ldmsd_cfgobj_first_re(cfgobj_type, regex);
		while (obj) {
			/* This must remove the cfgobj from the tree */
			result = obj->delete(obj);
			rc = ldmsd_reply_result_add(reply, obj->name, result);
			if (rc) {
				ldmsd_cfg_unlock(cfgobj_type);
				goto err;
			}
			ldmsd_cfgobj_put(obj); /* Put the find reference */
			obj = ldmsd_cfgobj_next_re(obj, regex);
		}
		ldmsd_cfg_unlock(cfgobj_type);
	}
out:
	ldmsd_req_buf_free(buf);
	return reply;
oom:
	rc = ENOMEM;
err:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (reply)
		json_entity_free(reply);
	errno = rc;
	return NULL;
}

static json_entity_t
ldmsd_cfgobj_query_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc;
	int msg_no = reqc->key.msg_no;
	ldmsd_cfgobj_t obj;
	json_entity_t schema, key, item, reply, result;
	char *schema_s, *name_s, *regex_s;
	schema_s = name_s = regex_s = NULL;
	enum ldmsd_cfgobj_type type;

	schema = json_value_find(reqc->json, "schema");
	if (!schema) {
		reply = ldmsd_reply_new("query", msg_no, EINVAL,
				"'schema' is missing.", NULL);
		if (!reply)
			goto oom;
		return reply;
	}
	schema_s = json_value_str(schema)->str;
	type = ldmsd_cfgobj_type_str2enum(schema_s);
	key = json_value_find(reqc->json, "key");

	reply = ldmsd_reply_new("query", msg_no, 0, NULL, NULL);
	if (!reply)
		goto oom;
	ldmsd_cfg_lock(type);
	if (key) {
		for (item = json_item_first(key); item; item = json_item_next(item)) {
			name_s = json_value_str(item)->str;
			obj = ldmsd_cfgobj_find(name_s, type);
			if (!obj) {
				result = ldmsd_result_new(ENOENT, NULL, NULL);
			} else {
				result = obj->query(obj);
			}
			if (!result) {
				ldmsd_cfg_unlock(type);
				goto oom;
			}
			rc = ldmsd_reply_result_add(reply, name_s, result);
			if (rc) {
				ldmsd_cfg_unlock(type);
				goto oom;
			}
		}
	} else {
		for (obj = ldmsd_cfgobj_first(type); obj;
				obj = ldmsd_cfgobj_next(obj)) {
			result = obj->query(obj);
			if (!result) {
				ldmsd_cfg_unlock(type);
				goto oom;
			}
			rc = ldmsd_reply_result_add(reply, obj->name, result);
			if (rc) {
				ldmsd_cfg_unlock(type);
				goto oom;
			}
		}
	}
	ldmsd_cfg_unlock(type);
	return reply;
oom:
	if (reply)
		json_entity_free(reply);
	errno = ENOMEM;
	return NULL;
}

static json_entity_t
ldmsd_cfgobj_export_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc;
	int msg_no = reqc->key.msg_no;
	json_entity_t schema, key, item, reply = NULL, result;
	char *schema_s, *name_s;
	ldmsd_cfgobj_t obj;
	int cfgobj_type;
	ldmsd_req_buf_t buf;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	schema = json_value_find(reqc->json, "schema");
	if (!schema) {
		reply = ldmsd_reply_new("export", msg_no, EINVAL,
					"'schema' is missing.", NULL);
		if (!reply)
			goto oom;
		return reply;
	}
	schema_s = json_value_str(schema)->str;
	cfgobj_type = ldmsd_cfgobj_type_str2enum(schema_s);
	if (cfgobj_type < 0) {
		rc = ldmsd_req_buf_append(buf, "schema '%s' not supported.", schema_s);
		if (rc < 0)
			goto oom;
		reply = ldmsd_reply_new("export", msg_no, ENOTSUP, buf->buf, NULL);
		if (!reply)
			goto oom;
		return reply;
	}
	key = json_value_find(reqc->json, "key");

	reply = ldmsd_reply_new("export", msg_no, 0, NULL, NULL);
	if (!reply)
		goto oom;

	ldmsd_cfg_lock(cfgobj_type);
	if (key) {
		for (item = json_item_first(key); item; item = json_item_next(item)) {
			name_s = json_value_str(key)->str;
			obj = ldmsd_cfgobj_find(name_s, cfgobj_type);
			if (!obj) {
				result = ldmsd_result_new(ENOENT, NULL, NULL);
			} else {
				result = obj->export(obj);
			}
			if (!result) {
				ldmsd_cfg_unlock(cfgobj_type);
				goto oom;
			}
			rc = ldmsd_reply_result_add(reply, obj->name, result);
			if (rc) {
				ldmsd_cfg_unlock(cfgobj_type);
				goto oom;
			}
		}
	} else {
		for (obj = ldmsd_cfgobj_first(cfgobj_type); obj; obj = ldmsd_cfgobj_next(obj)) {
			result = obj->export(obj);
			if (!result) {
				ldmsd_cfg_unlock(cfgobj_type);
				goto oom;
			}
			rc = ldmsd_reply_result_add(reply, obj->name, result);
			if (rc) {
				ldmsd_cfg_unlock(cfgobj_type);
				goto oom;
			}
		}
	}
	ldmsd_cfg_unlock(cfgobj_type);
	ldmsd_req_buf_free(buf);
	return reply;
oom:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (reply)
		json_entity_free(reply);
	errno = ENOMEM;
	return NULL;
}

static int stream_republish_cb(ldmsd_stream_client_t c, void *ctxt,
			       ldmsd_stream_type_t stream_type,
			       const char *data, size_t data_len,
			       json_entity_t entity)
{
	int rc;
	char *s;
	size_t s_len;
	jbuf_t jb = NULL;
	const char *stream = ldmsd_stream_client_name(c);

	if (data) {
		s = (char *)data;
		s_len = data_len;
	} else {
		jb = json_entity_dump(NULL, entity);
		if (!jb) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return ENOMEM;
		}
		s = jb->buf;
		s_len = jb->buf_len;
	}
	rc = ldmsd_stream_publish((ldms_t)ctxt, stream, stream_type, s, s_len);
	if (jb)
		jbuf_free(jb);
	return rc;
}

static json_entity_t
stream_subscribe_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int msg_no = reqc->key.msg_no;
	json_entity_t reply = NULL, streams, item;
	char *stream_name;
	ldmsd_stream_client_t c;

	streams = json_value_find(reqc->json, "stream_names");
	if (!streams) {
		reply = ldmsd_reply_new("stream_subscribe", msg_no, EINVAL,
				"'stream_subscribe' is missing.", NULL);
		if (!reply)
			goto oom;
		return reply;
	}

	if (JSON_LIST_VALUE != json_entity_type(streams)) {
		reply = ldmsd_reply_new("stream_subscribe", msg_no, EINVAL,
				"stream_subscribe: 'stream_names' must be a list.",
				NULL);
		if (!reply)
			goto oom;
		return reply;
	}

	for (item = json_item_first(streams); item; item = json_item_next(item)) {
		if (JSON_STRING_VALUE != json_entity_type(item)) {
			reply = ldmsd_reply_new("stream_subscribe", msg_no, EINVAL,
					"stream_subscribe: The elements of "
					"'stream_names' must be a string.", NULL);
			if (!reply)
				goto oom;
			return reply;
		}
		stream_name = json_value_str(item)->str;
		c = ldmsd_stream_subscribe(stream_name, stream_republish_cb,
					   reqc->xprt->ldms.ldms);
		if (!c)
			goto oom;
	}

	return ldmsd_reply_new("stream_subscribe", msg_no, 0, NULL, NULL);
oom:
	if (reply)
		json_entity_free(reply);
	errno = ENOMEM;
	return NULL;
}

static json_entity_t
version_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int msg_no = reqc->key.msg_no;
	json_entity_t reply, result = NULL;
	struct ldms_version ldms_version;
	struct ldmsd_version ldmsd_version;
	char ldms_v[1024], ldmsd_v[1024];

	ldms_version_get(&ldms_version);
	ldmsd_version_get(&ldmsd_version);

	snprintf(ldms_v, 1024, "%hhu.%hhu.%hhu.%hhu",
			ldms_version.major, ldms_version.minor,
			ldms_version.patch, ldms_version.flags);
	snprintf(ldmsd_v, 1024, "%hhu.%hhu.%hhu.%hhu",
			ldmsd_version.major, ldmsd_version.minor,
			ldmsd_version.patch, ldmsd_version.flags);

	result = json_dict_build(NULL,
				JSON_STRING_VALUE, "LDMS Version", ldms_v,
				JSON_STRING_VALUE, "LDMSD Version", ldmsd_v,
				-1);
	if (!result)
		goto oom;

	reply = ldmsd_reply_new("version", msg_no, 0, NULL, result);
	if (!reply)
		goto oom;
	return reply;

oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	errno = ENOMEM;
	if (result)
		json_entity_free(result);
	return NULL;
}

struct set_route_ctxt {
	ldmsd_req_ctxt_t original_reqc;
	json_entity_t info;
};

static int
set_route_resp_handler(ldmsd_req_ctxt_t reqc, void *resp_args)
{
	struct set_route_ctxt *ctxt = (struct set_route_ctxt *)resp_args;
	json_entity_t result, info, hop, a, reply;
	int status, rc;

	info = ctxt->info;

	status = json_value_int(json_value_find(reqc->json, "status"));
	result = json_value_find(reqc->json, "result");

	for(hop = json_attr_first(result); hop; hop = json_attr_next(hop)) {
		a = json_entity_copy(hop);
		if (!a) {
			json_entity_free(info);
			rc = ENOMEM;
			goto out;
		}
		json_attr_add(info, a);
	}

	reply = ldmsd_reply_new("set_route", ctxt->original_reqc->key.msg_no,
				status, NULL, info);
	rc = ldmsd_reply_send(ctxt->original_reqc, reply);

out:
	ldmsd_req_ctxt_ref_put(ctxt->original_reqc, "set_route_request");
	json_entity_free(reply);
	return rc;
}

static int
set_route_request(ldmsd_prdcr_t prdcr, ldmsd_req_ctxt_t original_reqc,
			char *inst_name, json_entity_t route, int nxt_hop_pos)
{
	int rc;
	struct set_route_ctxt *ctxt;
	json_entity_t req_obj = NULL;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return ENOMEM;

	ldmsd_req_ctxt_ref_get(original_reqc, "set_route_request");
	ctxt->original_reqc = original_reqc;
	ctxt->info = route;

	req_obj = ldmsd_req_obj_new("set_route");
	if (!req_obj)
		goto oom;

	req_obj = json_dict_build(req_obj,
			JSON_DICT_VALUE, "spec",
				JSON_STRING_VALUE, "instance", inst_name,
				JSON_INT_VALUE, "hop_postition", nxt_hop_pos,
				-2,
			-1);
	if (!req_obj)
		goto oom;

	rc = ldmsd_request_send(prdcr->xprt, req_obj,
				set_route_resp_handler, (void *)ctxt);
	if (rc)
		goto err;
	return 0;

oom:
	rc = ENOMEM;
err:
	if (req_obj)
		json_entity_free(req_obj);
	return rc;

}

static json_entity_t
set_route_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc;
	const char *REQ_NAME = "set_route";
	json_entity_t reply, spec, name, hop_pos, hop = NULL;
	char *inst_name, pos_str[1024];
	int pos;
	ldmsd_set_info_t info = NULL;
	uint32_t msg_no = reqc->key.msg_no;

	spec = json_value_find(reqc->json, "spec");
	if (!spec) {
		reply = ldmsd_reply_new(REQ_NAME, msg_no, EINVAL,
					"'spec' is missing.", NULL);
		goto out;
	}
	name = json_value_find(spec, "instance");
	if (!name) {
		reply = ldmsd_reply_new(REQ_NAME, msg_no, EINVAL,
					"'instance' is missing from 'spec'.",
					NULL);
		goto out;
	}
	if (JSON_STRING_VALUE != json_entity_type(name)) {
		reply = ldmsd_reply_new(REQ_NAME, msg_no, EINVAL,
				"'instance' must be the JSON string of "
				"a set instance name", NULL);
		goto out;
	}
	inst_name = json_value_str(name)->str;

	hop_pos = json_value_find(spec, "hop");
	if (hop_pos) {
		if (JSON_INT_VALUE != json_entity_type(hop_pos)) {
			reply = ldmsd_reply_new(REQ_NAME, msg_no, EINVAL,
				"'hop' must be an integer.", NULL);
			goto out;
		}

		pos = json_value_int(hop_pos);
	} else {
		pos = 0;
	}
	snprintf(pos_str, 1024, "%d", pos);

	if (0 == pos) {
		/* Add instance name */
		hop = json_dict_build(NULL,
				JSON_STRING_VALUE, "instance", inst_name,
				-1);
		if (!hop)
			goto oom;
	}

	info = ldmsd_set_info_get(inst_name);
	if (!info) {
		/* Add the ENOENT error message and send the reply back */
		hop = json_dict_build(hop,
				JSON_DICT_VALUE, pos_str,
					JSON_STRING_VALUE, "name", ldmsd_myname_get(),
					JSON_STRING_VALUE, "msg", "The set does not exist.",
					-2,
				-1);
		if (!hop)
			goto oom;
		reply = ldmsd_reply_new(REQ_NAME, msg_no, ENOENT, NULL, hop);
		goto out;
	}

	if (0 == pos) {
		/* Add schema name */
		hop = json_dict_build(hop,
			JSON_STRING_VALUE, "schema", ldms_set_schema_name_get(info->set),
			-1);
		if (!hop)
			goto oom;
	}

	/* Fill the info */
	if (info->origin_type == LDMSD_SET_ORIGIN_PRDCR) {
		hop = json_dict_build(hop,
				JSON_DICT_VALUE, pos_str,
					JSON_STRING_VALUE, "name", ldmsd_myname_get(),
					JSON_STRING_VALUE, "type", "aggregated set",
					JSON_STRING_VALUE, "producer", info->origin_name,
					-2,
				-1);
		if (!hop)
			goto oom;

		/* Forward the request to the producer */
		rc = set_route_request(info->prd_set->prdcr, reqc,
					inst_name, hop, pos + 1);
		if (rc) {
			reply = ldmsd_reply_new(REQ_NAME, msg_no, rc,
				"Error %d: failed to forward the request to the producer.",
				NULL);
			goto out;
		}
		reply = NULL;
		/*
		 * The reply will be ready when this LDMSD receives
		 * the response from the producer.
		 */
		errno = EINPROGRESS;
	} else {
		hop = json_dict_build(hop,
				JSON_DICT_VALUE, pos_str,
					JSON_STRING_VALUE, "name", ldmsd_myname_get(),
					JSON_STRING_VALUE, "type", "sampled set",
					JSON_STRING_VALUE, "plugin", info->origin_name,
					-2,
				-1);
		if (!hop)
			goto oom;
		reply = ldmsd_reply_new(REQ_NAME, msg_no, 0, NULL, hop);
		if (!reply)
			goto oom;
	}

out:
	return reply;
oom:
	if (info)
		ldmsd_set_info_delete(info);
	errno = ENOMEM;
	return NULL;
}

static json_entity_t
__test_protocol_forward_reply(json_entity_t reply, int hop_id)
{
	json_entity_t hops;
	char hop_id_s[8];

	hops = json_value_find(reply, "hops");
	if (!hops) {
		reply = json_dict_build(reply, JSON_DICT_VALUE, "hops", -2, -1);
		if (!reply)
			return NULL;
		hops = json_value_find(reply, "hops");
	}

	snprintf(hop_id_s, 8, "%d", hop_id);
	hops = json_dict_build(hops,
			JSON_STRING_VALUE, hop_id_s, ldmsd_myname_get(), -1);
	if (!hops) {
		json_entity_free(reply);
		return NULL;
	}
	return reply;
}

struct test_protocol_ctxt {
	json_entity_t reply;
	ldmsd_req_ctxt_t client_reqc;
};

static int
test_protocol_forward_resp_handler(ldmsd_req_ctxt_t reqc, void *args)
{
	int rc;
	json_entity_t reply, hops, rsp_hops;
	struct test_protocol_ctxt *ctxt = (struct test_protocol_ctxt *)args;

	reply = ctxt->reply;
	hops = json_value_find(reply, "hops");

	rc = json_value_int(json_value_find(reqc->json, "status"));
	if (rc) {
		rc = ldmsd_reply_error_set(reply, rc, NULL);
		if (rc)
			return ENOMEM;
	}

	rsp_hops = json_value_find(reqc->json, "hops");
	if (rsp_hops) {
		rc = json_dict_merge(hops, rsp_hops);
		if (rc)
			return ENOMEM;
	}
	rc = ldmsd_reply_send(ctxt->client_reqc, reply);
	ldmsd_req_ctxt_ref_put(ctxt->client_reqc, "ctxt_alloc");
	free(ctxt);
	return rc;
}

static int
__test_protocol_forward(ldmsd_req_ctxt_t reqc, json_entity_t reply,
			int num_hops, int hop_id)
{
	int rc;
	struct test_protocol_ctxt *ctxt;
	ldmsd_prdcr_t prdcr;
	json_entity_t req;

	prdcr = ldmsd_prdcr_first();
	if (!prdcr)
		return ENOENT;
	ldmsd_prdcr_get(prdcr);
	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return ENOMEM;
	ctxt->reply = reply;
	ldmsd_req_ctxt_ref_get(reqc, "ctxt_alloc");
	ctxt->client_reqc = reqc;

	req = ldmsd_req_obj_new("test_protocol");
	if (!req)
		goto oom;

	req = json_dict_build(req,
				JSON_STRING_VALUE, "mode", "forward",
				JSON_INT_VALUE, "num_hops", num_hops,
				JSON_INT_VALUE, "hop_id", hop_id,
				-1);
	if (!req)
		goto oom;

	rc = ldmsd_request_send(prdcr->xprt, req,
				test_protocol_forward_resp_handler, (void *)ctxt);
	if (rc)
		goto err;
	return 0;
oom:
	rc = ENOMEM;
err:
	free(ctxt);
	ldmsd_req_ctxt_ref_put(reqc, "ctxt_alloc");
	return rc;
}

static int
__test_protocol_forward_handler(ldmsd_req_ctxt_t reqc, json_entity_t reply)
{
	int rc;
	json_entity_t num_hops, hop_id;
	int num = INT8_MAX, id = 0;

	num_hops = json_value_find(reqc->json, "num_hops");
	if (num_hops) {
		if (JSON_INT_VALUE != json_entity_type(num_hops)) {
			return ldmsd_reply_error_set(reply, EINVAL,
					"'num_hops' must be an integer.");
		}
		num = json_value_int(num_hops);
	}

	hop_id = json_value_find(reqc->json, "hop_id");
	if (hop_id) {
		if (JSON_INT_VALUE != json_entity_type(hop_id)) {
			return ldmsd_reply_error_set(reply, EINVAL,
					"'hop_id' must be an integer.");
		}
		id = json_value_int(hop_id);
	}

	reply = __test_protocol_forward_reply(reply, id);
	if (!reply)
		goto oom;

	if (0 == num) {
		/*
		 * Don't forward the request.
		 */
		goto out;
	} else {
		/*
		 * Forward the request to the first producer.
		 */
		rc = __test_protocol_forward(reqc, reply, num - 1, id + 1);
		if (rc) {
			if (ENOMEM == rc) {
				goto oom;
			} else if (ENOENT == rc) {
				/*
				 * No more producers to forward the request to
				 */
				goto out;
			} else {
				reply = json_dict_build(reply,
						JSON_INT_VALUE, "status", rc,
						JSON_STRING_VALUE, "msg",
							"Failed to forward the request.",
						-1);
				if (!reply)
					goto oom;
			}
			rc = ldmsd_reply_error_set(reply, rc, NULL);
			if (rc)
				goto oom;
			goto out;
		} else {
			/*
			 * The reply is not ready yet.
			 */
			return EINPROGRESS;
		}
	}
out:
	return 0;

oom:
	if (reply)
		json_entity_free(reply);
	return ENOMEM;
}

static int
__test_protocol_multi_rec_rsp_handler(ldmsd_req_ctxt_t reqc, json_entity_t reply)
{
	int rc, i;
	json_entity_t num_rec, item, records;
	int num;
	size_t max_sz = reqc->xprt->max_msg;
	size_t hdr_sz = sizeof(struct ldmsd_rec_hdr_s);
	size_t remaining;
	jbuf_t jb;
	ldmsd_req_buf_t buf = NULL;

	num_rec = json_value_find(reqc->json, "num_records");
	if (!num_rec) {
		return ldmsd_reply_error_set(reply, EINVAL,
				"'num_records' is required.");
	}
	if (JSON_INT_VALUE != json_entity_type(num_rec)) {
		return ldmsd_reply_error_set(reply, EINVAL,
				"'num_records' must be an integer.");
	}

	num = json_value_int(num_rec);
	reply = json_dict_build(reply, JSON_LIST_VALUE, "records", -2, -1);
	if (!reply)
		goto oom;

	records = json_value_find(reply, "records");
	jb = json_entity_dump(NULL, reply);
	if (!jb)
		goto oom;

	buf = ldmsd_req_buf_alloc(max_sz);
	if (!buf) {
		jbuf_free(jb);
		goto oom;
	}

	for (i = 1; i <= num; i++) {
		if (1 == i) {
			/* +2 for ]} counted in jb->buf_len */
			remaining = max_sz - hdr_sz - jb->buf_len + 2;
			jbuf_free(jb);
		} else {
			/* -1 for , */
			remaining = max_sz -hdr_sz - 1;
		}
		if (num == i) {
			remaining -= 2; /* -2 for ]} */
		}
		rc = ldmsd_req_buf_append(buf, "rec_%d%*s", i, (int)remaining, "");
		if (rc < 0)
			goto oom;
		item = json_entity_new(JSON_STRING_VALUE, buf->buf);
		if (!item)
			goto oom;
		json_item_add(records, item);
		ldmsd_req_buf_reset(buf);
	}

	ldmsd_req_buf_free(buf);
	return 0;
oom:
	rc = ENOMEM;
	if (reply)
		json_entity_free(reply);
	if (buf)
		ldmsd_req_buf_free(buf);
	return rc;
}

static int
__test_protocol_long_rsp_handler(ldmsd_req_ctxt_t reqc, json_entity_t reply)
{
	int rc;
	json_entity_t len;
	long msg_sz, l, hdr_sz;
	jbuf_t jb;
	ldmsd_req_buf_t buf;

	hdr_sz = sizeof(struct ldmsd_rec_hdr_s);

	len = json_value_find(reqc->json, "length");
	if (!len)
		return ldmsd_reply_error_set(reply, EINVAL, "'length' is missing.");

	if (JSON_INT_VALUE != json_entity_type(len)) {
		return ldmsd_reply_error_set(reply, EINVAL,
						"'length' must be an integer.");
	}

	l = json_value_int(len);

	reply = json_dict_build(reply,
				JSON_INT_VALUE, "length", l,
				JSON_STRING_VALUE, "str", "",
				-1);
	if (!reply)
		goto oom;

	jb = json_entity_dump(NULL, reply);
	if (!jb)
		goto oom;

	buf = ldmsd_req_buf_alloc(l);
	if (!buf) {
		jbuf_free(jb);
		goto oom;
	}

	msg_sz = l - (hdr_sz + jb->cursor);
	if (msg_sz <= 0) {
		rc = ldmsd_req_buf_append(buf, "%ld", msg_sz);
	} else {
		rc = ldmsd_req_buf_append(buf, "(%ld+%d+%ld)", hdr_sz, jb->cursor, msg_sz);
		if (rc < 0)
			goto oom;
		rc = ldmsd_req_buf_append(buf, "%0*d", (int)(msg_sz - buf->off), 0);
	}
	if (rc < 0)
		goto oom;

	reply = json_dict_build(reply, JSON_STRING_VALUE, "str", buf->buf, -1);
	if (!reply)
		goto oom;

	jbuf_free(jb);
	ldmsd_req_buf_free(buf);
	return 0;

oom:
	rc = ENOMEM;
	if (reply)
		json_entity_free(reply);
	return rc;
}

static int
__test_protocol_echo_handler(ldmsd_req_ctxt_t reqc, json_entity_t reply)
{
	int rc;
	json_entity_t l, echo, a;

	l = json_value_find(reqc->json, "list");
	if (!l) {
		return ldmsd_reply_error_set(reply, EINVAL,
			"'list' is required for mode 'echo'.");
	}

	if (JSON_LIST_VALUE != json_entity_type(l)) {
		return ldmsd_reply_error_set(reply, EINVAL,
				"'list' must be a list.");
	}

	echo = json_entity_copy(l);
	if (!echo)
		goto oom;

	a = json_entity_new(JSON_ATTR_VALUE, "echo", echo);
	if (!a)
		goto oom;

	json_attr_add(reply, a);

	return 0;
oom:
	rc = ENOMEM;
	if (reply)
		json_entity_free(reply);
	return rc;
}

static json_entity_t
test_protocol_handler(ldmsd_req_ctxt_t reqc, struct ldmsd_sec_ctxt *sctxt)
{
	int rc;
	int msg_no = reqc->key.msg_no;
	json_entity_t reply, mode;
	json_str_t mode_s;

	reply = ldmsd_reply_new("test_protocol", msg_no, 0, NULL, NULL);
	if (!reply)
		goto oom;

	mode = json_value_find(reqc->json, "mode");
	if (!mode) {
		rc = ldmsd_reply_error_set(reply, EINVAL, "'mode' is missing.");
		if (rc)
			goto oom;
		return reply;
	}

	if (JSON_STRING_VALUE != json_entity_type(mode)) {
		rc = ldmsd_reply_error_set(reply, EINVAL, "'mode' must be a string.");
		if (rc)
			goto oom;
		return reply;
	}

	mode_s = json_value_str(mode);
	if (0 == strncmp(mode_s->str, "echo", mode_s->str_len)) {
		rc = __test_protocol_echo_handler(reqc, reply);
	} else if (0 == strncmp(mode_s->str, "long_rsp", mode_s->str_len)) {
		rc = __test_protocol_long_rsp_handler(reqc, reply);
	} else if (0 == strncmp(mode_s->str, "multi_rec_rsp", mode_s->str_len)) {
		rc = __test_protocol_multi_rec_rsp_handler(reqc, reply);
	} else if (0 == strncmp(mode_s->str, "forward", mode_s->str_len)) {
		rc = __test_protocol_forward_handler(reqc, reply);
	} else {
		rc = ldmsd_reply_error_set(reply, ENOTSUP, "Not supported mode");
	}
	if (rc)
		goto err;
	return reply;
oom:
	rc = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
err:
	errno = rc;
	return NULL;
}
