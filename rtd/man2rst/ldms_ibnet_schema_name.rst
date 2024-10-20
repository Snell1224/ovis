======================
ldms_ibnet_schema_name
======================

:Date: 4 June 2020

.. contents::
   :depth: 3
..

NAME
=======================

ldms_ibnet_schema_name - man page for the LDMS ibnet plugin support
utility

SYNOPSIS
===========================

ldms_ibnet_schema_name <plugin config options>

DESCRIPTION
==============================

The ibnet plugin generates a schema name including a hash of certain
configuration data. ldms_ibnet_schema_name provides the user with the
resulting name before running ldmsd so that store plugins can be
configured.

CONFIGURATION ATTRIBUTE SYNTAX
=================================================

See Plugin_ibnet(7).

EXAMPLES
===========================

::

   ldms_ibnet_schema_name node-name-map=/path/map timing=2 metric-conf=/path/metricsubsets schema=myibnet

   when file /path/metricsubsets contains

   extended
   xmtsl
   rcvsl
   xmtdisc
   rcverr
   oprcvcounters
   flowctlcounters
   vloppackets
   vlopdata
   vlxmitflowctlerrors/t
   vlxmitcounters/t
   swportvlcong
   rcvcc/t
   slrcvfecn
   slrcvbecn
   xmitcc/t
   vlxmittimecc
   smplctl/t

   yields

   myibnet_7fffe_tn

NOTES
========================

If the timing option is greater than 0, the name of the overall timing
set will be as for the result given with "\_timing" appended.

SEE ALSO
===========================

Plugin_ibnet(7)
