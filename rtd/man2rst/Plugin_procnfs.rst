==============
Plugin_procnfs
==============

:Date: 10 Feb 2018

.. contents::
   :depth: 3
..

NAME
===============

Plugin_procnfs - man page for the LDMS procnfs plugin

SYNOPSIS
===================

| Within ldmsd_controller or a configuration file:
| config name=procnfs [ <attr>=<value> ]

DESCRIPTION
======================

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The procnfs plugin provides info from
/proc/net/rpc/nfs

CONFIGURATION ATTRIBUTE SYNTAX
=========================================

The procnfs plugin uses the sampler_base base class. This man page
covers only the configuration attributes, or those with default values,
specific to the this plugin; see ldms_sampler_base.man for the
attributes of the base class.

**config**
   | name=<plugin_name> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be procnfs.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`procnfs\`.

BUGS
===============

No known bugs.

EXAMPLES
===================

::

   Within ldmsd_controller or a configuration file:
   load name=procnfs
   config name=procnfs producer=vm1_1 instance=vm1_1/procnfs
   start name=procnfs interval=1000000

SEE ALSO
===================

ldmsd(8), ldms_quickstart(7), ldmsd_controller(8), ldms_sampler_base(7)
