/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011 Sandia Corporation. All rights reserved.
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
 * \file meminfo.c
 * \brief /proc/meminfo data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"


#define PROC_FILE "/proc/meminfo"

static char *procfile = PROC_FILE;
static uint64_t counter;
ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
uint64_t comp_id;
static char *qc_dir = NULL;
static int qc_file = -1;

#ifdef HAVE_QC_SAMPLER
static int get_qc_file(const char *qc_dir);
#endif

static int create_metric_set(const char *path)
{
#ifdef HAVE_QC_SAMPLER
	qc_file = get_qc_file(qc_dir);
#endif
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, metric_count;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open the meminfo file '%s'...exiting\n", procfile);
		return ENOENT;
	}

	metric_count = 0;
	tot_meta_sz = 0;
	tot_data_sz = 0;

	/* First iteration for set size calculation. */
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64, metric_name,
			    &metric_value);
		if (rc < 2)
			break;
		/* Strip the colon from metric name if present */
		i = strlen(metric_name);
		if (i && metric_name[i-1] == ':')
			metric_name[i-1] = '\0';

		rc = ldms_get_metric_size(metric_name, LDMS_V_U64,
					  &meta_sz, &data_sz);
		if (rc)
			return rc;

		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		metric_count++;
	} while (s);

	/* Create the metric set */
	rc = ENOMEM;
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;
	/*
	 * Process the file again to define all the metrics.
	 */

	int metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64,
			    metric_name, &metric_value);
		if (rc < 2)
			break;
		/* Strip the colon from metric name if present */
		i = strlen(metric_name);
		if (i && metric_name[i-1] == ':')
			metric_name[i-1] = '\0';

		metric_table[metric_no] =
			ldms_add_metric(set, metric_name, LDMS_V_U64);
		if (!metric_table[metric_no]) {
			rc = ENOMEM;
			goto err;
		}
		ldms_set_user_data(metric_table[metric_no], comp_id);
		metric_no++;
	} while (s);
	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

/**
 * \brief Configuration
 *
 * config name=meminfo component_id=<comp_id> set=<setname>
 *        qc_log_dir=<qc_log_directory>
 *     comp_id     The component id value.
 *     setname     The set name.
 *     qc_log_dir  The QC data file directory.
 *                 This option is only relevant if --enable-qc-sampler
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	/* if user does not specify qc_log_dir
	 then log a message and continue     */
	value = av_value(avl, "qc_log_dir");
	if (value) {
		qc_dir = strdup(value);
	}

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;

	if (!set) {
		msglog("meminfo: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);

	#ifdef HAVE_QC_SAMPLER
		/* get current date & time */
		char date_and_time[64];
		char qc_buffer[1024];
		int qc_buffer_size = 1024;
		if (qc_file != -1) {
			struct timeval my_time;
			gettimeofday(&my_time, NULL);
			snprintf(date_and_time,64,"%ld.%ld",
                                my_time.tv_sec, my_time.tv_usec);
			date_and_time[63]='\0';
			snprintf(qc_buffer,qc_buffer_size,
				"%s,%s,%llu\n",
				"#time",
				date_and_time,
				date_and_time);
			qc_buffer[qc_buffer_size-1] = '\0';
			write(qc_file,	qc_buffer, strlen(qc_buffer));			
		}
	#endif


	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &v.v_u64);
		if (rc != 2 && rc != 3) {
			rc = EINVAL;
			goto out;
		}

		ldms_set_metric(metric_table[metric_no], &v);

		#ifdef HAVE_QC_SAMPLER
			/* write a metric to the qc data file */
			if (qc_file != -1) {
				snprintf(qc_buffer,qc_buffer_size,
					"%s,%s,%llu\n",
					metric_name,
					date_and_time,
					(unsigned long long)v.v_u64);
				qc_buffer[qc_buffer_size-1] = '\0';
				write(qc_file,	qc_buffer, strlen(qc_buffer));
			}
		#endif


		metric_no++;
	} while (s);

	#ifdef HAVE_QC_SAMPLER
		/* flush qc file */
        	fsync(qc_file);
	#endif

 out:
	ldms_end_transaction(set);
	return 0;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;

	if (qc_dir != NULL)
		free(qc_dir);
	qc_dir = NULL;

}

static const char *usage(void)
{
	return "config name=meminfo component_id=<comp_id> set=<setname> "
			"qc_log_dir=<qc_log_directory>\n"
			"    comp_id     The component id value.\n"
			"    setname     The set name.\n"
			"    qc_log_dir  The QC data file directory.\n";
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = "meminfo",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &meminfo_plugin.base;
}

#ifdef HAVE_QC_SAMPLER
static void close_qc_file()
{
	close(qc_file);
	qc_file = -1;
	free(qc_dir);
	qc_dir = NULL;
}

/**
 * Open a QC output file.
 * The name of the file is QC_[hostname]_[comp_id]_[random chars].txt.
 * @param qc_dir The directory that will contain the file.
 * If qc_dir is NULL, then -1 is returend.
 * @return the file handler.  If an error occurrs, -1 will be returned.
 */
static int get_qc_file(const char *qc_dir)
{
	if (qc_dir==NULL) {
		errno = ENOENT;
		return(-1);
	}

	/* does the directory exists? */
	struct stat s;
	int err = stat(qc_dir, &s);
	if (-1 == err) {
		errno = ENOENT;
		return (-1);
	} else {
		if (!S_ISDIR(s.st_mode)) {
			errno = ENOTDIR;
			return (-1);
		}
	}

	/* get hostname */
	char hostname[128] = "";
	if (gethostname(hostname, 128) != 0) 
		strcpy(hostname,"unknown");
	hostname[127] = '\0';

	/* truncate hostname to first dot */
	char *ptr = strchr(hostname,'.');
        if (ptr!=NULL)
		*ptr = '\0';

	/* filename is QC_[hostname]_[comp_id]_[random chars].txt */
	char qc_filename[128];
	snprintf(qc_filename, 128, "%s/QC_%s_%llu_meminfo_%s.txt",
			qc_dir, hostname,
			(unsigned long long) comp_id, "XXXXXX");
        qc_filename[127] = '\0';
	int qc_file = mkstemps(qc_filename, 4);
	if (qc_file == -1) {
		errno = EIO;
		return (-1);
	}

	return (qc_file);
}
#endif

