=========================
Plugin_loadavg
=========================

:Date: 7 Apr 2020

.. contents::
   :depth: 3
..

NAME
==========================

Plugin_loadavg - Plugin_loadavg page for the LDMS loadavg plugin

SYNOPSIS
==============================

| Within ldmsd_controller
| config name=loadavg [ <attr> = <value> ]

DESCRIPTION
=================================

The loadavg plugin provides OS information from /proc/loadavg

CONFIGURATION ATTRIBUTE SYNTAX
====================================================

This plugin uses the sampler_base base class. This man page covers only
the configuration attributes, or those with default values, specific to
the this plugin; see ldms_sampler_base.man for the attributes of the
base class.

**config**
   name=<plugin_name> [schema=<sname>] [metrics=<mlist>] [force_integer]

   name=<plugin_name>
      |
      | This MUST be loadavg.

   force_integer
      |
      | If present, this flag forces load metrics to be stored as
        integers of 100*value provided in the proc file.

   schema=<schema>
      |
      | Optional schema name. If schema is not specified, it will be
        computed. The default name is loadavg if the metrics option is
        not supplied. The default name when metrics is specified is
        loadavgXXXXXX, where each X corresponds to whether or not that
        metric is included. When force_integer is configured, the
        loadavg prefix becomes loadavgi.

   metrics=<mlist>
      |
      | comma separated list of metrics to include. If not given, all
        are included. The complete list is load1min, load5min,
        load15min, runnable, scheduling_entities, newest_pid.

DATA
==========================

This reports metrics from /proc/loadavg, which has the format: load1min
load5min load15min runnable/scheduling_entities newest_pid.

The load numbers are multiplied by 100 and cast to unsigned integers as
they are collected, rather than being collected as real numbers.

EXAMPLES
==============================

Within ldmsd_controller or a configuration file:

::

   load name=loadavg
   config name=loadavg producer=vm1_1 component_id=1 instance=vm1_1/loadavg
   start name=loadavg interval=1000000

NOTES
===========================

See proc(5) for the definitions of the metrics.

SEE ALSO
==============================

proc(5), ldmsd(8), ldms_sampler_base(7), ldmsd_controller(8)
