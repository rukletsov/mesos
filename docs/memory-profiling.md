---
title: Apache Mesos - Memory Profiling
layout: documentation
---

# Memory profiling with Mesos and Jemalloc

On Linux systems, Mesos is able to leverage the memory-profiling
capabilities of the jemalloc [1] general-purpose allocator to
provide powerful debugging tools for investigating memory-related
issues.

These include detailed real-time statistics of the current memory
usage, as well as information about the location and frequency
of individual allocations.

This generally works by having libprocess detect at run-time whether
the current process is using jemalloc as its memory allocator,
and if so enable a number of HTTP endpoints described below that
allow operators to generate the desired data at runtime.

[1] http://jemalloc.net


# Requirements

There are two ways to enjoy the memory profiling features of libprocess.

The recommended method is to specify the `--enable-jemalloc-allocator`
compile-time flag, which causes the `mesos-master` and `mesos-agent` binaries to
be statically linked against a bundled version of jemalloc will be
compiled with the correct flags.

If a suitable version of jemalloc is already present, it is of course also
possible to use that instead by pointing `--with-jemalloc=` to it, where
suitable means that it should have been built with the `--enable-stats`
and `--enable-prof` flags.

The second way is to use the `LD_PRELOAD` mechanism to preload
a `libjemalloc.so` shared library that is present on the system at run-time.
The MemoryProfiler class in libprocess will automatically detect this and
enable its memory profiling support.

The generated profile dumps will be written to a random directory
under `TMPDIR` if set, otherwise in a subdirectory of `/tmp`.


# Usage

There are two independent sets of data that can be collected from `jemalloc`,
memory statistics and heap profiling information.


## Memory statistics

The `/statistics` endpoint returns exact statistics about the memory usage
in JSON format, for example the number of bytes currently allocated and the
size distribution of these allocations.

It takes no parameters and will return the results in JSON format.

        http://example.org:5050/memory-profiler/statistics

Be aware that the returned JSON is quite large, so when accessing this
endpoint from a terminal, it is advisable to redirect the results into
a file.


## Heap profiling

The profiling done by jemalloc works by sampling from the calls to `malloc()`
according to a configured probability distribution, and storing stack
traces for the sampled calls in a separate memory area. These can then
be dumped into files on the filesystem, so-called heap profiles.

In general, to start a profiling run one would access the `/start` endpoint

        http://example.org:5050/memory-profiler/start?duration=5mins

followed by downloading one of the generated files described below after
the duration has elapsed. The remaining time of the current profiling run
can be verified at the `/state` endpoint.

The profile collection can also be stopped early with `/stop` endpoint:

        http://example.org:5050/memory-profiler/stop

To analyze the generated profiling data, the results are offered in three
different formats.

  1) Raw profile

        http://example.org:5050/memory-profiler/download/raw

     This returns a file in a plain text format containing the raw backtraces
     collected , i.e. lists of memory addresses. It can be interactively analyzed
     and rendered using the `jeprof` tool provided by the jemalloc project.
     For more information on this file format, see also

       http://jemalloc.net/jemalloc.3.html#heap_profile_format

  2) Symbolized profile

        http://example.org:5050/memory-profiler/download/text

     This is similar to the raw format above, except that jeprof is called on
     the host machine to attempt to read debug information

     Usage of this endpoint requires that jeprof is present on the host machine
     and on the `PATH`, and no useful information will be generated unless the
     binary contains symbol information.

  2) Call graph

        http://example.org:5050/memory-profiler/download/graph

     This endpoint returns an image in svg format that shows a graphical
     representation of the samples backtraces.

     Usage of this endpoint requires that jeprof is present on the host machine
     and on the `PATH` of mesos, and no useful information will be generated
     unless the binary contains symbol information.


Which of these is needed will depend on the circumstances of the application
deployment and of the bug that is investigated.

For example, the call graph presents information in a visual, immediately useful
form, but is difficult to filter and post-process if non-default output options
are desired.

On the other hand, in many debian-like environments symbol information is by default
stripped from binaries to save space and shipped in separate packages. In such an
environment, if it is not permitted to install additional packages on the host
running Mesos, one would store the raw profiles and enrich them with symbol information
locally.


# Command-line usage

It may be more convenient to automate the downloading of heap profiles by
writing a simple script. A simple example for how this might look like
is

    #!/bin/bash

    SECONDS=600
    HOST=example.org:5050

    curl ${HOST}/memory-profiler/start?duration=${SECONDS}
    sleep $((${SECONDS} + 1))
    wget ${HOST}/memory-profiler/download/raw

A more sophisticated script would additionally store the `id` value
returned by the call to `/start` and pass it as a paremter to `/download`,
to ensure that a new run was not started in the meantime.


# Using `MALLOC_CONF` as alternative interface to `jemalloc`

The jemalloc allocator natively provides an alternative interface to
control the memory profiling behaviour. The usual way to provide settings
through this interface is by setting the environment variable `MALLOC_CONF`.

This interface provides a number of options that are not exposed by Libprocess,
like generating heap profiles automatically after a certain amount of memory has
been allocated, or whenever memory usage reaches a new high-water mark. The full
list of settings is described on the jemalloc man page [2].

On the other hand, features like starting and stopping the profiling at run-time
or getting the information provided by the `/statistics` endpoint can not be
achieved through the `MALLOC_CONF` interface.

For example, to create a dump automatically for every 1 GiB worth of recorded
allocations, one might use the configuration:

    MALLOC_CONF="prof:true,prof_prefix:/path/to/folder,lg_prof_interval=20"

To debug memory allocations during early startup, profiling can be activated
before accessing the `/start` endpoint:

    MALLOC_CONF="prof:true,prof_active:true"

[2] https://www.freebsd.org/cgi/man.cgi?jemalloc(3)


# Disabling memory profiling

For various reasons, it can be desired to use jemalloc as a memory
allocator without necessarily wanting to make use of the memory profiling
features described above. For example, our testing indicates some performance
benefits over glibc's default ptmalloc.

All run-time features described in this document can be disabled by
specifying the `--memory_profiling=false` flag of the `mesos-master` and
`mesos-agent` binaries or by setting the corresponding environment
variable `LIBPROCESS_MEMORY_PROFILING=false`.
