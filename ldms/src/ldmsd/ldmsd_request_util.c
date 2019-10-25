/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <ovis_util/util.h>
#include "ldmsd.h"
#include "ldmsd_request.h"

/*
 * \c key_ is the place holder of the key value to be returned.
 */
void ldmsd_msg_key_get(void *xprt, struct ldmsd_msg_key *key_)
{
	static uint32_t msg_no = 0;
	key_->msg_no = __sync_fetch_and_add(&msg_no, 1);
	key_->conn_id = (uint64_t)(unsigned long)xprt;
}

size_t __get_remaining(size_t max_msg, ldmsd_req_buf_t buf)
{
	size_t remaining;

	if (max_msg < buf->len)
		remaining = max_msg;
	else
		remaining = buf->len;
	/* Guarantee that 0 <= returned value <= xprt->max_msg and buf->len */
	return remaining - buf->off;
}

int ldmsd_append_msg_buffer(void *xprt, size_t max_msg, struct ldmsd_msg_key *key,
			ldmsd_msg_send_fn_t send_fn,
			ldmsd_req_buf_t buf,
			int msg_flags, int msg_type,
			const char *data, size_t data_len)
{
	ldmsd_rec_hdr_t req_buff;
	size_t remaining;
	int flags, rc;

	req_buff = (ldmsd_rec_hdr_t)buf->buf;
	if (0 == buf->off) {
		/* This is a new buffer. Set the offset to the header size. */
		buf->off = sizeof(struct ldmsd_rec_hdr_s);
	}

	do {
		remaining = __get_remaining(max_msg, buf);
		if (data_len < remaining)
			remaining = data_len;

		if (remaining && data) {
			memcpy(&buf->buf[buf->off], data, remaining);
			buf->off += remaining;
			data_len -= remaining;
			data += remaining;
		}

		if ((remaining == 0) ||
		    ((data_len == 0) && (msg_flags & LDMSD_REC_EOM_F))) {
			/* If this is the first record in the response, set the
			 * SOM_F bit. If the caller set the EOM_F bit and we've
			 * exhausted data_len, set the EOM_F bit.
			 * If we've exhausted the reply buffer, unset the EOM_F bit.
			 */
			flags = msg_flags & ((!remaining && data_len)?(~LDMSD_REC_EOM_F):LDMSD_REC_EOM_F);
			flags |= (buf->num_rec == 0?LDMSD_REC_SOM_F:0);
			/* Record is full, send it on it's way */
			req_buff->type = msg_type;
			req_buff->flags = flags;
			req_buff->key = *key;
			req_buff->rec_len = buf->off;
			ldmsd_hton_rec_hdr(req_buff);
			rc = send_fn(xprt, (char *)req_buff, ntohl(req_buff->rec_len));
			if (rc) {
				/* The content in reqc->rep_buf hasn't been sent. */
				ldmsd_log(LDMSD_LERROR, "failed to send the reply of "
						"the config request %d from "
						"config xprt id %" PRIu64 "\n",
						key->msg_no, key->conn_id);
				return rc;
			}
			buf->num_rec++;
			/* Reset the reply buffer for the next record for this message */
			buf->off = sizeof(*req_buff);
			buf->buf[buf->off] = '\0';
		}
	} while (data_len);

	return 0;
}

int ldmsd_append_msg_buffer_va(void *xprt, size_t max_msg, struct ldmsd_msg_key *key,
			ldmsd_msg_send_fn_t send_fn,
			ldmsd_req_buf_t buf,
			int msg_flags, int msg_type, const char *fmt, ...)
{
	char *str = NULL;
	va_list ap;
	size_t cnt;

	va_start(ap, fmt);
	cnt = vsnprintf(str, 0, fmt, ap);
	va_end(ap);
	str = malloc(cnt + 1);
	if (!str)
		return ENOMEM;
	va_start(ap, fmt);
	cnt = vsnprintf(str, cnt + 1, fmt, ap);
	va_end(ap);
	return ldmsd_append_msg_buffer(xprt, max_msg, key, send_fn,
				buf, msg_flags, msg_type, str, cnt);
}

void ldmsd_ntoh_rec_hdr(ldmsd_rec_hdr_t req)
{
	req->type = ntohl(req->type);
	req->flags = ntohl(req->flags);
	req->key.msg_no = ntohl(req->key.msg_no);
	req->key.conn_id = be64toh(req->key.conn_id);
	req->rec_len = ntohl(req->rec_len);
}

void ldmsd_hton_rec_hdr(ldmsd_rec_hdr_t req)
{
	req->flags = htonl(req->flags);
	req->key.msg_no = htonl(req->key.msg_no);
	req->key.conn_id = htobe64(req->key.conn_id);
	req->rec_len = htonl(req->rec_len);
	req->type = htonl(req->type);
}

ldmsd_req_buf_t ldmsd_req_buf_alloc(size_t len)
{
	ldmsd_req_buf_t buf = malloc(sizeof(struct ldmsd_req_buf));
	if (!buf)
		return NULL;
	buf->buf = malloc(len);
	if (!buf) {
		free(buf);
		return NULL;
	}
	buf->len = len;
	buf->off = 0;
	buf->buf[0] = '\0';
	buf->num_rec = 0;
	return buf;
}

ldmsd_req_buf_t ldmsd_req_buf_realloc(ldmsd_req_buf_t buf, size_t new_len)
{
	buf->buf = realloc(buf->buf, new_len);
	if (!buf->buf) {
		free(buf);
		return NULL;
	}
	buf->len = new_len;
	return buf;
}

void ldmsd_req_buf_reset(ldmsd_req_buf_t buf)
{
	buf->off = 0;
	buf->buf[0] = '\0';
	buf->num_rec = 0;
}

void ldmsd_req_buf_free(ldmsd_req_buf_t buf)
{
	free(buf->buf);
	free(buf);
}
