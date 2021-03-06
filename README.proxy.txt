FLEXNBD-PROXY(1)
================
:doctype: manpage

NAME
----

flexnbd-proxy - A simple NBD proxy

SYNOPSIS
--------

*flexnbd-proxy* ['OPTIONS']

DESCRIPTION
-----------

flexnbd-proxy is a simple NBD proxy server that implements resilient
connection logic for the client. It connects to an upstream NBD server
and allows a single client to connect to it. All server properties are
proxied to the client, and the client connection is kept alive across
reconnections to the upstream server. If the upstream goes away while
an NBD request is in-flight then the proxy (silently, from the point
of view of the client) reconnects and retransmits the request, before
returning the response to the client.

USAGE
-----

  $ flexnbd-proxy --addr <ADDR> [ --port <PORT> ]
    --conn-addr <ADDR> --conn-port <PORT> 
    [--bind <ADDR>] [--cache[=<CACHE_BYTES>]] [option]*

Proxy requests from an NBD client to an NBD server, resiliently. Only one
client can be connected at a time, and ACLs cannot be applied to the client, as they 
can be to clients connecting directly to a flexnbd in serve mode.

On starting up, the proxy will attempt to connect to the server specified by
--conn-addr and --conn-port (from the address specified by --bind, if given). If
it fails, then the process will die with an error exit status.

Assuming a successful connection to the `upstream` server is made, the proxy
will then start listening on the address specified by --addr and --port, waiting
for `downstream` to connect to it (this will be your NBD client). The client
will be given the same hello message as the proxy was given by the server.

When connected, any request the client makes will be read by the proxy and sent
to the server. If the server goes away for any reason, the proxy will remember
the request and regularly (~ every 5 seconds) try to reconnect to the server.
Upon reconnection, the request is sent and a reply is waited for. When a reply
is received, it is sent back to the client.

When the client disconnects, cleanly or otherwise, the proxy goes back to
waiting for a new client to connect. The connection to the server is maintained
at that point, in case it is needed again.

Only one request may be in-flight at a time under the current architecture; that
doesn't seem to slow things down much relative to alternative options, but may
be changed in the future if it becomes an issue.

Options
~~~~~~~

*--addr, -l ADDR*:
    The address to listen on. If this begins with a '/', it is assumed to be
    a UNIX domain socket to create. Otherwise, it should be an IPv4 or IPv6
    address.
*--port, -p PORT*:
    The port to listen on, if --addr is not a UNIX socket. 

*--conn-addr, -C ADDR*:
    The address of the NBD server to connect to. Required.

*--conn-port, -P PORT*:
    The port of the NBD server to connect to. Required.

*--cache, -c=CACHE_BYTES*:
    If given, the size in bytes of read cache to use. CACHE_BYTES
    defaults to 4096.

*--help, -h* :
    Show command or global help.

*--verbose, -v* :
    Output all available log information to STDERR.

*--quiet, -q* :
    Output as little log information as possible to STDERR.


LOGGING
-------
Log output is sent to STDERR.  If --quiet is set, no output will be seen
unless the program termintes abnormally.  If neither --quiet nor
--verbose are set, no output will be seen unless something goes wrong
with a specific request.  If --verbose is given, every available log
message will be seen (which, for a debug build, is many).  It is not an
error to set both --verbose and --quiet.  The last one wins.

The log line format is:

  <TIMESTAMP>:<LEVEL>:<PID> <THREAD> <SOURCEFILE>:<SOURCELINE>: <MSG>

*TIMESTAMP*:
  Time the log entry was made. This is expressed in terms of monotonic ms

*LEVEL*:
  This will be one of 'D', 'I', 'W', 'E', 'F' in increasing order of
severity.  If flexnbd is started with the --quiet flag, only 'F' will be
seen.  If it is started with the --verbose flag, any from 'I' upwards
will be seen.  Only if you have a debug build and start it with
--verbose will you see 'D' entries.

*PID*:
  This is the process ID.

*THREAD*:
  flexnbd-proxy is currently single-threaded, so this should be the same
for all lines. That may not be the case in the future.

*SOURCEFILE:SOURCELINE*:
  Identifies where in the source code this log line can be found.

*MSG*:
  A short message describing what's happening, how it's being done, or
if you're very lucky *why* it's going on.

Proxying
~~~~~~~~

The main point of the proxy mode is to allow clients that would otherwise break
when the NBD server goes away (during a migration, for instance) to see a
persistent TCP connection throughout the process, instead of needing its own
reconnection logic.

For maximum reliability, the proxy process would be run on the same machine as
the actual NBD client; an example might look like:

  nbd-server-1$ flexnbd serve -l 10.0.0.1 -p 4777 myfile [...]

  nbd-client-1$ flexnbd-proxy -l 127.0.0.1 -p 4777 -C 10.0.0.1 -P 4777
  nbd-client-1$ nbd-client -c 127.0.0.1 4777 /dev/nbd0

  nbd-server-2$ flexnbd listen -l 10.0.0.2 -p 4777 -f myfile [...]

  nbd-server-1$ flexnbd mirror --addr 10.0.0.2 -p 4777 [...]

Upon completing the migration, the mirroring and listening flexnbd servers will
both exit. With the proxy mediating requests, this does not break the TCP
connection that nbd-client is holding open. If no requests are in-flight, it
will not notice anything at all; if requests are in-flight, then the reply may
take longer than usual to be returned.

When flexnbd is restarted in serve mode on the second server:

  nbd-server-2$ flexnbd serve -l 10.0.0.1 -p 4777 -f myfile [...]

The proxy notices and reconnects, fulfiling any request it has in its buffer.
The data in myfile has been moved between physical servers without the nbd
client process having to be disturbed at all.

READ CACHE
----------

If the --cache option is given at the command line, either without an
argument or with an argument greater than 0, flexnbd-proxy will use a
read-ahead cache.  The cache as currently implemented doubles each read
request size, up to a maximum of 2xCACHE_BYTES, and retains the latter
half in a buffer.  If the next read request from the client exactly
matches the region held in the buffer, flexnbd-proxy responds from the
cache without making a request to the server.

This pattern is designed to match sequential reads, such as those
performed by a booting virtual machine.

Note: If specifying a cache size, you *must* use this form:

  nbd-client$ flexnbd-proxy --cache=XXXX

That is, the '=' is required.  This is a limitation of getopt-long.

If no cache size is given, a size of 4096 bytes is assumed. Caching can
be explicitly disabled by setting a size of 0.

BUGS
----

Should be reported to nick@bytemark.co.uk.

Current issues include:

* Only old-style NBD negotiation is supported
* Only one request may be in-flight at a time
* All I/O is blocking, and signals terminate the process immediately
* UNIX socket support is limited to the listen address
* FLUSH and TRIM commands, and the FUA flag, are not supported
* DISCONNECT requests do not get passed through to the NBD server
* No active timeout-retry of requests - we trust the kernel's idea of failure

AUTHOR
------
Written by Alex Young <alex@bytemark.co.uk>.
Original concept and core code by Matthew Bloch <matthew@bytemark.co.uk>.
Proxy mode written by Nick Thomas <nick@bytemark.co.uk>

COPYING
-------

Copyright (c) 2012 Bytemark Hosting Ltd. Free use of this software is
granted under the terms of the GNU General Public License version 3 or
later.

