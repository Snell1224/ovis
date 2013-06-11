/*
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
 * \file lustre_oss.c
 * \brief Lustre OSS data sampler.
 * \author Narate Taerat <narate@ogc.us>
 *
 * This plugin samples data from multiple OSTs in the OSS it is running on.
 * The plugin will sample stats in OSTs according to the targets given at
 * configuration time. If no target is given, it will listen to OSTs that are
 * available at the time. Please see configure() for more information about this
 * plugin configuration.
 *
 * This plugin gets its content from:
 * <code>
 * /proc/fs/lustre/ost/OSS/<service>/stats
 * /proc/fs/lustre/obdfilter/lustre-OSTXXXX/stats
 * </code>
 *
 */


#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"

#include "str_map.h"
#include "lustre_sampler.h"

#define STR_MAP_SIZE 4093

/**
 * These are the services under /proc/fs/lustre/oss/OSS/
 */
char *oss_services[] = {
	"ost",
	"ost_create",
	"ost_io",
	"ost_seq"
};
#define OSS_SERVICES_LEN (__ALEN(oss_services))

/**
 * This will holds IDs for stats_key.
 */
struct str_map *stats_key_id;

struct lustre_svc_stats_head svc_stats = {0};

static uint64_t counter;
static ldms_set_t set;
FILE *mf;
ldmsd_msg_log_f msglog;
union ldms_value comp_id;
ldms_metric_t compid_metric_handle;
ldms_metric_t counter_metric_handle;

char tmp_path[PATH_MAX];


/**
 * \brief Construct string list out of a given comma-separated list of OSTs.
 *
 * \param osts The comma-separated list of OSTs.
 * \returns NULL on error.
 * \returns ::str_list_head pointer on success.
 */
struct str_list_head* construct_ost_list(const char *osts)
{
	if (osts)
		/* OSTs is given */
		return construct_str_list(osts);
	else
		/* OSTs is not given, get current ones from proc fs */
		return construct_dir_list("/proc/fs/lustre/obdfilter");
}

/**
 * \brief Create metric set.
 *
 * Currently, lustre_oss samples all possible metrics in the given OSTs.
 * In the future, it will support metric filtering too.
 *
 * \param path The set name, e.g. vic1/lustre_oss (it does look like a path).
 * \param osts The comma-separated list of OSTs. NULL means all OSTs available
 * 	at the time.
 *
 * \returns 0 on success.
 * \returns \c errno on error.
 */
static int create_metric_set(const char *path, const char *osts)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, metric_count;
	uint64_t metric_value;
	char metric_name[128];

	/* First calculate the set size */
	metric_count = 0;
	tot_meta_sz = tot_data_sz = 0;
	/* for comp_id and count */
	ldms_get_metric_size("component_id", LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;
	metric_count++;
	ldms_get_metric_size("oss_counter", LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;
	metric_count++;
	/* Calculate size for OSS */
	for (i=0; i<OSS_SERVICES_LEN; i++) {
		for (j=0; j<STATS_KEY_LEN; j++) {
			sprintf(metric_name, "oss.%s.%s", oss_services[i],
					stats_key[j]);
			ldms_get_metric_size(metric_name, LDMS_V_U64,
						  &meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	/* Calculate size for OSTs */
	struct str_list_head *lh = construct_ost_list(osts);
	if (!lh)
		goto err0;
	struct str_list *sl;
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		for (j=0; j<STATS_KEY_LEN; j++) {
			sprintf(metric_name, "ost.%s.%s", sl->str,
					stats_key[j]);
			ldms_get_metric_size(metric_name, LDMS_V_U64,
					     &meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	/* Done calculating, now it is time to construct set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		goto err1;
	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle)
		goto err2;
	counter_metric_handle = ldms_add_metric(set, "oss_counter", LDMS_V_U64);
	if (!counter_metric_handle)
		goto err2;
	char name_base[128];
	for (i=0; i<OSS_SERVICES_LEN; i++) {
		sprintf(tmp_path, "/proc/fs/lustre/ost/OSS/%s/stats",
				oss_services[i]);
		sprintf(name_base, "oss.%s.stats", oss_services[i]);
		rc = stats_construct_routine(set, tmp_path, name_base,
					     &svc_stats, stats_key,
					     STATS_KEY_LEN, stats_key_id);
		if (rc)
			goto err2;
	}
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		sprintf(tmp_path, "/proc/fs/lustre/obdfilter/%s/stats", sl->str);
		sprintf(name_base, "ost.%s.stats", sl->str);
		rc = stats_construct_routine(set, tmp_path, name_base,
					     &svc_stats, stats_key,
					     STATS_KEY_LEN, stats_key_id);
		if (rc)
			goto err2;
	}

	return 0;
err2:
	msglog("lustre_oss.c:create_metric_set@err2\n");
	lustre_svc_stats_list_free(&svc_stats);
	ldms_destroy_set(set);
	msglog("WARNING: lustre_oss set DESTROYED\n");
	set = 0;
err1:
	msglog("lustre_oss.c:create_metric_set@err1\n");
	free_str_list(lh);
err0:
	msglog("lustre_oss.c:create_metric_set@err0\n");
	return errno;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

/**
 * \brief Configuration
 *
 * (ldmsctl usage note)
 * <code>
 * config name=meminfo component_id=<comp_id> set=<setname> osts=<OST1>,...
 *     comp_id     The component id value.
 *     setname     The set name.
 *     osts        The comma-separated list of the OSTs to sample from.
 * </code>
 * If osts is not given, the plugin will create ldms_set according to the
 * available OSTs at the time.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *osts;

	value = av_value(avl, "component_id");
	if (value)
		comp_id.v_u64 = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	osts = av_value(avl, "osts");
	if (value)
		create_metric_set(value, osts);

	return 0;
}

static const char *usage(void)
{
	return  "config name=meminfo component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	if (!set)
		return EINVAL;
	ldms_begin_transaction(set);

	/* Set the counter */
	union ldms_value v = {.v_u64 = ++counter};
	ldms_set_metric(counter_metric_handle, &v);

	/* Set metric id */
	ldms_set_metric(compid_metric_handle, &comp_id);

	struct lustre_svc_stats *lss;

	/* For all stats */
	LIST_FOREACH(lss, &svc_stats, link) {
		lss_sample(lss);
	}

 out:
	ldms_end_transaction(set);
	return 0;
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = "lustre_oss",
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
	lustre_sampler_set_msglog(pf);
	stats_key_id = str_map_create(STR_MAP_SIZE);
	if (!stats_key_id) {
		msglog("stats_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(stats_key_id, stats_key, STATS_KEY_LEN, 1);

	return &meminfo_plugin.base;
err_nomem:
	errno = ENOMEM;
	return NULL;
}
