/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
/**
 * \file butils.c
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 *
 * \brief Implementation of functions (and some global variables) defined in
 * butils.h
 */
#include "butils.h"
#include <linux/limits.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

FILE *blog_file;
pthread_mutex_t __blog_mutex = PTHREAD_MUTEX_INITIALIZER;
int __blog_level;

void __attribute__ ((constructor)) butils_init();
void butils_init()
{
	static int visited = 0;
	if (visited)
		return;
	visited = 1;
	blog_file = stderr;
}

void blog_set_level(int level)
{
	__blog_level = level;
}

const char *__level_lbl[] = {
	[BLOG_LV_DEBUG] = "DEBUG",
	[BLOG_LV_INFO] = "INFO",
	[BLOG_LV_WARN] = "WARN",
	[BLOG_LV_ERR] = "ERROR",
	[BLOG_LV_QUIET] = "QUIET",
};

int blog_set_level_str(const char *level)
{
	int i, rc;
	int n, len;
	/* Check if level is pure number */
	n = 0;
	sscanf(level, "%d%n", &i, &n);
	len = strlen(level);
	if (n == len) {
		blog_set_level(i);
		return 0;
	}
	for (i = 0; i < BLOG_LV_LAST; i++) {
		rc = strncmp(level, __level_lbl[i], len);
		if (rc == 0) {
			blog_set_level(i);
			return 0;
		}
	}
	return EINVAL;
}

int blog_get_level()
{
	return __blog_level;
}

void blog_set_file(FILE *f)
{
	blog_file = f;
	/* redirect stdout and stderr to this file too */
	dup2(fileno(f), 1);
	dup2(fileno(f), 2);
}

int blog_open_file(const char *path)
{
	FILE *f = fopen(path, "a");
	if (!f)
		return errno;
	blog_set_file(f);
	return 0;
}

int blog_close_file()
{
	return fclose(blog_file);
}

void __blog(const char *fmt, ...)
{
	pthread_mutex_lock(&__blog_mutex);
	va_list ap;
	char date[32];
	time_t t = time(NULL);
	ctime_r(&t, date);
	date[24] = 0;
	fprintf(blog_file, "%s ", date);
	va_start(ap, fmt);
	vfprintf(blog_file, fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&__blog_mutex);
	fflush(blog_file);
}

int blog_flush()
{
	return fflush(blog_file);
}

int blog_rotate(const char *path)
{
	int rc = 0;
	FILE *new_log = fopen(path, "a");
	if (!new_log)
		return errno;

	pthread_mutex_lock(&__blog_mutex);
	dup2(fileno(new_log), 1);
	dup2(fileno(new_log), 2);

	blog_flush();
	blog_close_file();

	blog_file = new_log;
	pthread_mutex_unlock(&__blog_mutex);
	return rc;
}

int bfile_exists(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	return !rc;
}

int bis_dir(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	if (rc == -1)
		return 0;
	rc = S_ISDIR(st.st_mode);
	if (!rc) {
		errno = EINVAL;
	}
	return rc;
}

int bmkdir_p(const char *path, __mode_t mode)
{
	char *str = strdup(path);
	char *_str;
	int rc = 0;
	if (!str)
		return ENOMEM;
	_str = str;
	int len = strlen(str);
	if (str[len-1] == '/') {
		len--;
		str[len] = 0;
	}
	if (_str[0] == '/')
		_str++; /* skip the leading '/' */
	while ((_str = strstr(_str, "/"))) {
		*_str = 0;
		if (!bfile_exists(str)) {
			if (mkdir(str, mode) == -1) {
				rc = errno;
				goto cleanup;
			}
		}
		if (!bis_dir(str)) {
			errno = ENOTDIR;
			rc = ENOTDIR;
			goto cleanup;
		}
		*_str = '/';
		_str++;
	}
	rc = mkdir(str, 0755);
	if (rc)
		rc = errno;
cleanup:
	free(str);
	return rc;
}

struct bdstr* bdstr_new(size_t len)
{
	if (!len)
		len = 4096;
	struct bdstr *s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	s->str = malloc(len);
	if (!s->str) {
		free(s);
		return NULL;
	}
	s->alloc_len = len;
	s->str_len = 0;
	s->str[0] = '\0';
	return s;
}

int bdstr_expand(struct bdstr *bs, size_t new_size)
{
	char *new_str = realloc(bs->str, new_size);
	if (!new_str)
		return errno;
	bs->alloc_len = new_size;
	bs->str = new_str;
	return 0;
}

int bdstr_append(struct bdstr *bs, const char *str)
{
	int len = strlen(str);
	int rc;
	if (bs->str_len + len + 1 > bs->alloc_len) {
		int exp_len = (len | 0xFFF) + 1;
		rc = bdstr_expand(bs, bs->alloc_len + exp_len);
		if (rc)
			return rc;
	}
	strcat(bs->str + bs->str_len, str);
	bs->str_len += len;
	return 0;
}

int bdstr_append_bstr(struct bdstr *bdstr, const struct bstr *bstr)
{
	int rc;
	if (bdstr->str_len + bstr->blen + 1 > bdstr->alloc_len) {
		int exp_len = (bstr->blen | 0xFFF) + 1;
		rc = bdstr_expand(bdstr, bdstr->alloc_len + exp_len);
		if (rc)
			return rc;
	}
	strncpy(bdstr->str + bdstr->str_len, bstr->cstr, bstr->blen);
	bdstr->str_len += bstr->blen;
	bdstr->str[bdstr->str_len] = 0;
	return 0;
}

int bdstr_append_printf(struct bdstr *bdstr, const char *fmt, ...)
{
	int rc = 0;
	va_list ap;
	char *str;
	size_t sz;
	int n;
again:
	str = bdstr->str + bdstr->str_len;
	sz = bdstr->alloc_len - bdstr->str_len;
	va_start(ap, fmt);
	n = vsnprintf(str, sz, fmt, ap);
	va_end(ap);
	if (n >= sz) {
		int exp_len = bdstr->str_len + n + 1;
		exp_len = (exp_len | 0xFFF) + 1;
		bdstr->str[bdstr->str_len] = 0; /* recover old string */
		rc = bdstr_expand(bdstr, exp_len);
		if (rc)
			goto out;
		goto again;
	}
	bdstr->str_len += n;
out:
	return rc;
}

int bdstr_append_mem(struct bdstr *bdstr, void *mem, size_t len)
{
	int rc;
	if (bdstr->str_len + len + 1 > bdstr->alloc_len) {
		int exp_len = (len | 0xFFF) + 1;
		rc = bdstr_expand(bdstr, bdstr->alloc_len + exp_len);
		if (rc)
			return rc;
	}
	memcpy(bdstr->str + bdstr->str_len, mem, len);
	bdstr->str_len += len;
	bdstr->str[bdstr->str_len] = 0;
	return 0;
}

char *bdstr_detach_buffer(struct bdstr *bdstr)
{
	char *str = bdstr->str;
	bdstr->str_len = 0;
	bdstr->alloc_len = 0;
	bdstr->str = NULL;
	return str;
}

int bdstr_reset(struct bdstr *bdstr)
{
	bdstr->str_len = 0;
	if (!bdstr->str) {
		bdstr->str = malloc(4096);
		if (!bdstr->str)
			return ENOMEM;
	}
	bdstr->str[0] = 0;
	return 0;
}

void bdstr_free(struct bdstr *bdstr)
{
	if (bdstr->str)
		free(bdstr->str);
	free(bdstr);
}

int bstr_lev_dist_u32(const struct bstr *a, const struct bstr *b, void *buff,
								size_t buffsz)
{
	int i, j, d;
	int na = a->blen / sizeof(uint32_t);
	int nb = b->blen / sizeof(uint32_t);
	int *x0, *x1;
	void *tmp;

	if (na < nb) {
		d = na;
		na = nb;
		nb = d;
		tmp = (void*)a;
		a = b;
		b = tmp;
	}

	if (2*na*sizeof(*x0) > buffsz) {
		berr("%s: Not enough buffsz: %d, required: %d", __func__,
				buffsz, 2*na*sizeof(*x0));
		errno = ENOMEM;
		return -1;
	}

	x0 = buff;
	x1 = x0 + na;
	x0[0] = a->u32str[0] != b->u32str[0];
	for (i = 1; i < na; i++) {
		x0[i] = (a->u32str[i] == b->u32str[0])?(i):(x0[i-1] + 1);
	}

	for (j = 1; j < nb; j++) {
		x1[0] = (a->u32str[0] == b->u32str[j])?(j):(x0[0] + 1);
		for (i = 1; i < na; i++) {
			x1[i] = x0[i-1] + (a->u32str[i] != b->u32str[j]);
			d = 1 + BMIN(x1[i-1], x0[i]);
			x1[i] = BMIN(x1[i], d);
		}
		tmp = x0;
		x0 = x1;
		x1 = tmp;
	}

	return x0[na-1];
}

int bstr_lcs_u32(const struct bstr *a, const struct bstr *b, void *buff,
								size_t buffsz)
{
	int i, j, d;
	int na = a->blen / sizeof(uint32_t);
	int nb = b->blen / sizeof(uint32_t);
	int *x0, *x1;
	void *tmp;

	if (na < nb) {
		d = na;
		na = nb;
		nb = d;
		tmp = (void*)a;
		a = b;
		b = tmp;
	}

	if (2*na*sizeof(*x0) > buffsz) {
		berr("%s: Not enough buffsz: %d, required: %d", __func__,
				buffsz, 2*na*sizeof(*x0));
		errno = ENOMEM;
		return -1;
	}

	x0 = buff;
	x1 = x0 + na;
	x0[0] = a->u32str[0] == b->u32str[0];
	for (i = 1; i < na; i++) {
		x0[i] = (a->u32str[i] == b->u32str[0])?(1):(x0[i-1]);
	}

	for (j = 1; j < nb; j++) {
		x1[0] = (a->u32str[0] == b->u32str[j])?(1):(x0[0]);
		for (i = 1; i < na; i++) {
			x1[i] = BMAX(x1[i-1], x0[i]);
			if (a->u32str[i] == b->u32str[j])
				x1[i] = BMAX(x1[i], x0[i-1] + 1);
		}
		tmp = x0;
		x0 = x1;
		x1 = tmp;
	}

	return x0[na-1];
}

int bstr_lcsX_u32(const struct bstr *a, const struct bstr *b, int *idx,
					int *idx_len, void *buff, size_t buffsz)
{
	uint32_t *lcs = buff;
	int len_a = a->blen / sizeof(uint32_t);
	int len_b = b->blen / sizeof(uint32_t);
	int i, j, k;
	uint32_t max;
	int rc;

#define _LCS(x_a,y_b) lcs[(x_a) + (y_b)*len_a]

	if (buffsz < (len_a*len_b*sizeof(uint32_t)))
		return ENOMEM;

	if (*idx_len < len_a)
		return ENOMEM;

	_LCS(0, 0) = a->u32str[0] == b->u32str[0];
	for (i = 1; i < len_a; i++) {
		_LCS(i, 0) = (a->u32str[i] == b->u32str[0])?(1):(_LCS(i-1,0));
	}
	for (j = 1; j < len_b; j++) {
		_LCS(0, j) = (a->u32str[0] == b->u32str[j])?(1):(_LCS(0,j-1));
	}

	for (j = 1; j < len_b; j++) {
		for (i = 1; i < len_a; i++) {
			_LCS(i, j) = BMAX(_LCS(i-1,j), _LCS(i,j-1));
			if (a->u32str[i] == b->u32str[j])
				_LCS(i, j) = BMAX(1+_LCS(i-1,j-1), _LCS(i,j));
		}
	}

	i = len_a - 1;
	j = len_b - 1;
	k = _LCS(i, j);
	*idx_len = k;

	while (k) {
		if (i && _LCS(i, j) == (_LCS(i - 1, j))) {
			i--;
			continue;
		}

		if (j && _LCS(i, j) == (_LCS(i, j - 1))) {
			j--;
			continue;
		}

		idx[k - 1] = i;
		i--;
		j--;
		k--;
	}

	assert(i >= -1);
	assert(j >= -1);

#undef _LCS
	return rc;
}

int bstr_lcs_dist_u32(const struct bstr *a, const struct bstr *b, void *buff,
								size_t buffsz)
{
	int na = a->blen/sizeof(uint32_t);
	int nb = b->blen/sizeof(uint32_t);
	int lcs_len;
	lcs_len = bstr_lcs_u32(a, b, buff, buffsz);
	if (lcs_len < 0)
		return lcs_len;
	return na + nb - 2*lcs_len;
}

int bparse_http_query(const char *query, struct bpair_str_head *head)
{
	int rc = 0;
	char *p, *pp;
	const char *c = query;
	struct bpair_str *kv, *last_kv;
	char *key, *value;
	size_t len;
	char hex[3] = {0};

	last_kv = NULL;

loop:
	key = value = NULL;

	if (!*c)
		goto out;

	/* key */
	len = strcspn(c, "=");
	key = strndup(c, len);
	if (!key) {
		rc = ENOMEM;
		goto err;
	}
	c += len;
	if (!*c) {
		rc = EINVAL;
		goto err;
	}
	c++; /* skip the delim */

	/* value */
	len = strcspn(c, "&#");
	value = strndup(c, len);
	if (!value) {
		rc = ENOMEM;
		goto err;
	}
	c += len;
	if (*c) {
		c++; /* skip the delim */
	}
	pp = p = value;
	while (*p) {
		switch (*p) {
		case '+':
			*pp++ = ' ';
			p++;
			break;
		case '%':
			hex[0] = p[1];
			hex[1] = p[2];
			*pp++ = strtol(hex, NULL, 16);
			p += 3;
			break;
		default:
			*pp++ = *p++;
		}
	}
	*pp = 0;
	kv = bpair_str_alloc(key, value);
	if (!kv) {
		rc = ENOMEM;
		goto err;
	}

	/* put it in the list */
	if (last_kv) {
		LIST_INSERT_AFTER(last_kv, kv, link);
	} else {
		LIST_INSERT_HEAD(head, kv, link);
	}
	last_kv = kv;

	goto loop;

err:
	if (key)
		free(key);
	if (value)
		free(value);

	bpair_str_list_free(head);

out:
	return rc;
}

int bprocess_file_by_line(const char *path, bprocess_file_by_line_cb_t cb,
								void *ctxt)
{
	int rc = 0;
	FILE *fin = NULL;
	char *buff = NULL;
	size_t sz = 4096;

	fin = fopen(path, "r");
	if (!fin) {
		bwarn("Cannot open file: %s", path);
		rc = errno;
		goto out;
	}

	buff = malloc(sz);
	if (!buff) {
		rc = errno;
		goto out;
	}

	while (fgets(buff, sz, fin)) {
		rc = cb(buff, ctxt);
		if (rc)
			goto out;
	}

out:
	if (fin)
		fclose(fin);
	if (buff)
		free(buff);
	return rc;
}

struct __ctxt {
	bprocess_file_by_line_cb_t cb;
	void *ctxt;
};

static
int __bprocess_file_by_line_w_comment_cb(char *line, void *ctxt)
{
	struct __ctxt *c = ctxt;
	char *s = strchr(line, '#');
	if (s) {
		*s = 0;
	}
	if (s == line)
		return 0;
	s = line + strlen(line) - 1;
	while (s >= line && isspace(*s)) {
		*s = 0;
		s--;
	}
	if (s < line)
		return 0;
	return c->cb(line, c->ctxt);
}

int bprocess_file_by_line_w_comment(const char *path,
				bprocess_file_by_line_cb_t cb, void *ctxt)
{
	struct __ctxt c = {.cb = cb, .ctxt = ctxt};
	return bprocess_file_by_line(path, __bprocess_file_by_line_w_comment_cb, &c);
}

int bcsv_get_cell(const char *str, const char **end)
{
	int in_quote = 0;
	const char *s = str;
	static const char delim[256] = {
		['\n'] = 1,
		['\r'] = 1,
		[','] = 1,
	};

	while (*s && (in_quote || !delim[*s])) {
		if (*s == '"') {
			if (in_quote) {
				if (*(s+1) == '"') {
					/* "" in the quote, stays in_quote */
					s++;
				} else {
					/* end quote */
					in_quote = 0;
				}
			} else {
				in_quote = 1;
			}
		}
		s++;
	}

	*end = s;

	if (in_quote)
		return ENOENT;

	return 0;
}
/* END OF FILE */
