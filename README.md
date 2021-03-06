# Notice

`master` is a development branch. Please download release tar balls
in https://github.com/ovis-hpc/ovis/releases, or check out `OVIS-4` branch 
for the pre-release instead.

# Introduction

__OVIS__ is a modular system for HPC data collection, transport, storage,
log message exploration, and visualization as well as analysis.


## Lightweight Distributed Metric Service (LDMS)

LDMS is a low-overhead, low-latency framework for collecting, transfering, and
storing metric data on a large distributed computer system. The framework
includes

* a public API with a reference implementation,
* tools for collecting, aggregating, transporting, and storing metric values,
* collectors for several common types of metrics.
* Data transport over socket, RDMA (IB/iWarp/RoCE), and Cray Gemini as well as
  Aries.

The API provides a way for vendors to expose system information in a uniform
manner without being required to provide source code for accessing the
information (although we advise it be included) which might reveal proprietary
methods or information.

Metric information can be updated by a kernel module which runs only when
applications yield the processor and transported using RDMA-like operations,
resulting in minimal jitter during collection. LDMS has been run on 10,000 cores
collecting over 100,000 metric values per second with less than 0.2% overhead.

See [LDMS](ldms/README.md) for more details about LDMS project.


## Scalable Storage System (SOS)

SOS is a high-performance, indexed, object-oriented database designed to
efficiently manage structured data on persistent media. More information can be
found at the SOS GitHub website <https://github.com/opengridcomputing/SOS>.

## Baler

Baler is an aggregation of log message exploration and analysis tools. _balerd_
is the tool that tokenizes log messages using user-specified dictionaries. The
log messages are then groupped together according to their token sequences.
Each group is represented by a Baler pattern -- a token sequence.

Baler stores the log message patterns, the raw log messages, and other
infomation in its database. _bquery_ is a tool to query Baler database for the
Baler patterns, raw log messages, and message statistics by hosts and/or time.

Baler also comes with an association mining tool -- _bassoc_ -- that can be used
to discover sequence of occurrence patterns of log messages and to perform
causal analysis.

Baler repository can be found at <https://github.com/opengridcomputing/baler>.

# Obtaining OVIS source code

You may clone OVIS source (and its submodules) from the official
[Git](http://git-scm.com/) repository:

```sh
git clone https://github.com/ovis-hpc/ovis.git
```

# Dependencies

* autoconf (>=2.63), automake, libtool
* glib2
* libreadline
* openssl Development library for OVIS, LDMS Authentication
* For LDMS Python Interface:
	* Python-2.7.
	* swig
* doxygen if you want to build OVIS documentation.
* Some LDMS plug-ins have dependency on additional libraries. For cray-related
  LDMS sampler plug-in dependencies, please see the man page of the plug-in in
  `ldms/man/`.


# Building OVIS

At the OVIS top directory,

```sh
./autogen.sh
mkdir <build directory>
cd <build directory>
../configure --prefix=<installed path> [options]
make
make install
```

# Supported hardware

* Ubuntu and friends
* CentOS and friends
* Cray XE6, Cray XK, Cray XC
