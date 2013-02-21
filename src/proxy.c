#include "proxy.h"
#include "readwrite.h"

#include "ioutil.h"
#include "sockutil.h"
#include "util.h"

#include <errno.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

struct proxier* proxy_create(
	struct flexnbd* flexnbd,
	char* s_downstream_address,
	char* s_downstream_port,
	char* s_upstream_address,
	char* s_upstream_port,
	char* s_upstream_bind )
{
	NULLCHECK( flexnbd );

	struct proxier* out;
	out = xmalloc( sizeof( struct proxier ) );
	out->flexnbd = flexnbd;

	FATAL_IF_NULL(s_downstream_address, "Listen address not specified");
	NULLCHECK( s_downstream_address );

	FATAL_UNLESS(
		parse_ip_to_sockaddr( &out->listen_on.generic, s_downstream_address ),
		"Couldn't parse downstream address '%s' (use 0 if "
		"you want to bind all IPs)",
		s_downstream_address
	);

	FATAL_IF_NULL( s_downstream_port, "Downstream port not specified" );
	NULLCHECK( s_downstream_port );
	parse_port( s_downstream_port, &out->listen_on.v4 );

	FATAL_IF_NULL(s_upstream_address, "Upstream address not specified");
	NULLCHECK( s_upstream_address );

	FATAL_UNLESS(
		parse_ip_to_sockaddr( &out->connect_to.generic, s_upstream_address ),
		"Couldn't parse upstream address '%s'",
		s_upstream_address
	);

	FATAL_IF_NULL( s_upstream_port, "Upstream port not specified" );
	NULLCHECK( s_upstream_port );
	parse_port( s_upstream_port, &out->connect_to.v4 );

	if ( s_upstream_bind ) {
		FATAL_IF_ZERO(
			parse_ip_to_sockaddr( &out->connect_from.generic, s_upstream_bind ),
			"Couldn't parse bind address '%s'",
			s_upstream_bind
		);
	}

	out->listen_fd = -1;
	out->downstream_fd = -1;
	out->upstream_fd = -1;

	out->req_buf = xmalloc( NBD_MAX_SIZE );
	out->rsp_buf = xmalloc( NBD_MAX_SIZE );

	return out;
}

void proxy_destroy( struct proxier* proxy )
{
	free( proxy->req_buf );
	free( proxy->rsp_buf );
	free( proxy );
}


/* Try to establish a connection to our upstream server. Return 1 on success,
 * 0 on failure
 */
int proxy_connect_to_upstream( struct proxier* proxy )
{
	int fd = socket_connect( &proxy->connect_to.generic, &proxy->connect_from.generic );
	off64_t size = 0;

	if ( -1 == fd ) {
		return 0;
	}

	if( !socket_nbd_read_hello( fd, &size ) ) {
		FATAL_IF_NEGATIVE(
			close( fd ), SHOW_ERRNO( "FIXME: shouldn't be fatal" )
		);
		return 0;
	}

	if ( proxy->upstream_size == 0 ) {
		info( "Size of upstream image is %"PRIu64" bytes", size );
	} else if ( proxy->upstream_size != size ) {
		warn( "Size changed from %"PRIu64" to %"PRIu64" bytes", proxy->upstream_size, size );
	}

	proxy->upstream_size = size;
	proxy->upstream_fd = fd;

	return 1;
}

void proxy_disconnect_from_upstream( struct proxier* proxy )
{
	if ( -1 != proxy->upstream_fd ) {
		debug(" Closing upstream connection" );

		/* TODO: An NBD disconnect would be pleasant here */

		FATAL_IF_NEGATIVE(
			close( proxy->upstream_fd ),
			SHOW_ERRNO( "FIXME: shouldn't be fatal" )
		);
		proxy->upstream_fd = -1;
	}
}


/** Prepares a listening socket for the NBD server, binding etc. */
void proxy_open_listen_socket(struct proxier* params)
{
	NULLCHECK( params );

	params->listen_fd = socket(params->listen_on.family, SOCK_STREAM, 0);
	FATAL_IF_NEGATIVE(
		params->listen_fd, SHOW_ERRNO( "Couldn't create listen socket" )
	);

	/* Allow us to restart quickly */
	FATAL_IF_NEGATIVE(
		sock_set_reuseaddr(params->listen_fd, 1),
		SHOW_ERRNO( "Couldn't set SO_REUSEADDR" )
	);

	FATAL_IF_NEGATIVE(
		sock_set_tcp_nodelay(params->listen_fd, 1),
		SHOW_ERRNO( "Couldn't set TCP_NODELAY" )
	);

	FATAL_UNLESS_ZERO(
		sock_try_bind( params->listen_fd, &params->listen_on.generic ),
		SHOW_ERRNO( "Failed to bind to listening socket" )
	);

	/* We're only serving one client at a time, hence backlog of 1 */
	FATAL_IF_NEGATIVE(
		listen(params->listen_fd, 1),
		SHOW_ERRNO( "Failed to listen on listening socket" )
	);

	info( "Now listening for incoming connections" );
}


/* Return 0 if we should keep running, 1 if an exit has been signaled. Pass it
 * an fd_set to check, or set check_fds to NULL to have it perform its own.
 * If we do the latter, then wait specifies how many seconds we'll wait for an
 * exit signal to show up.
 */
int proxy_should_exit( struct proxier* params, fd_set *check_fds, int wait )
{
	struct timeval tv = { wait, 0 };
	fd_set internal_fds;
	fd_set* fds = check_fds;

	int signal_fd = flexnbd_signal_fd( params->flexnbd );

	if ( NULL == check_fds ) {
		fds = &internal_fds;

		FD_ZERO( fds );
		FD_SET( signal_fd, fds );

		FATAL_IF_NEGATIVE(
			sock_try_select(FD_SETSIZE, fds, NULL, NULL, &tv),
			SHOW_ERRNO( "select() failed." )
		);
	}

	if ( FD_ISSET( signal_fd, fds ) ) {
		info( "Stop signal received" );
		return 1;
	}

	return 0;
}

/* Try to get a request from downstream. If reading from downstream fails, then
 * the session will be over. Returns 1 on success, 0 on failure.
 */
int proxy_get_request_from_downstream( struct proxier* proxy )
{
	unsigned char* req_hdr_raw = proxy->req_buf;
	unsigned char* req_data = proxy->req_buf + NBD_REQUEST_SIZE;
	size_t req_buf_size;

	struct nbd_request_raw* request_raw = (struct nbd_request_raw*) req_hdr_raw;
	struct nbd_request*     request = &(proxy->req_hdr);

	if ( readloop( proxy->downstream_fd, req_hdr_raw, NBD_REQUEST_SIZE ) == -1 ) {
		info( SHOW_ERRNO( "Failed to get request header" ) );
		return 0;
	}

	nbd_r2h_request( request_raw, request );
	req_buf_size = NBD_REQUEST_SIZE;

	if ( request->type == REQUEST_DISCONNECT ) {
		info( "Received disconnect request from client" );
		return 0;
	}

	if ( request->type == REQUEST_READ ) {
		if (request->len > ( NBD_MAX_SIZE - NBD_REPLY_SIZE ) ) {
			warn( "NBD read request size %"PRIu32" too large", request->len );
			return 0;
		}

	}

	if ( request->type == REQUEST_WRITE ) {
		if (request->len > ( NBD_MAX_SIZE - NBD_REQUEST_SIZE ) ) {
			warn( "NBD write request size %"PRIu32" too large", request->len );
			return 0;
		}

		if ( readloop( proxy->downstream_fd, req_data, request->len ) == -1 ) {
			warn( "Failed to get NBD write request data: %"PRIu32"b", request->len );
			return 0;
		}

		req_buf_size += request->len;
	}

	debug(
		"Received NBD request from downstream. type=%"PRIu32" from=%"PRIu64" len=%"PRIu32,
		request->type, request->from, request->len
	);

	proxy->req_buf_size = req_buf_size;
	return 1;
}

/* Tries to send the request upstream and receive a response. If upstream breaks
 * then we reconnect to it, and keep it up until we have a complete response
 * back. Returns 1 on success, 0 on failure, -1 if exit is signalled.
 */
int proxy_run_request_upstream( struct proxier* proxy )
{
	unsigned char* rsp_hdr_raw  = proxy->rsp_buf;
	unsigned char* rsp_data = proxy->rsp_buf + NBD_REPLY_SIZE;

	struct nbd_reply_raw*   reply_raw   = (struct nbd_reply_raw*) rsp_hdr_raw;

	struct nbd_request*     request = &(proxy->req_hdr);
	struct nbd_reply*       reply = &(proxy->rsp_hdr);

	size_t rsp_buf_size;

	if ( proxy->upstream_fd == -1 ) {
		debug( "Connecting to upstream" );
		if ( !proxy_connect_to_upstream( proxy ) ) {
			debug( "Failed to connect to upstream" );

			if ( proxy_should_exit( proxy, NULL, 5 ) ) {
				return -1;
			}

			return 0;
		}
		debug( "Connected to upstream" );
	}

	if ( writeloop( proxy->upstream_fd, proxy->req_buf, proxy->req_buf_size ) == -1 ) {
		warn( "Failed to send request to upstream" );
		proxy_disconnect_from_upstream( proxy );
		return 0;
	}

	if ( readloop( proxy->upstream_fd, rsp_hdr_raw, NBD_REPLY_SIZE ) == -1 ) {
		debug( "Failed to get reply header from upstream" );
		proxy_disconnect_from_upstream( proxy );
		return 0;
	}

	nbd_r2h_reply( reply_raw, reply );
	rsp_buf_size = NBD_REPLY_SIZE;

	if ( reply->magic != REPLY_MAGIC ) {
		debug( "Reply magic is incorrect" );
		proxy_disconnect_from_upstream( proxy );
		return 0;
	}

	debug( "NBD reply received from upstream. Response code: %"PRIu32, reply->error );

	if ( reply->error != 0 ) {
		warn( "NBD  error returned from upstream: %"PRIu32, reply->error );
	}

	if ( reply->error == 0 && request->type == REQUEST_READ ) {
		if (readloop( proxy->upstream_fd, rsp_data, request->len ) == -1 ) {
			debug( "Failed to get reply data from upstream" );
			proxy_disconnect_from_upstream( proxy );
			return 0;
		}
		rsp_buf_size += request->len;
	}

	proxy->rsp_buf_size = rsp_buf_size;
	return rsp_buf_size;
}

/* Write an NBD reply back downstream. Return 0 on failure, 1 on success. */
int proxy_send_reply_downstream( struct proxier* proxy )
{
	int result;
	unsigned char* rsp_buf = proxy->rsp_buf;

	debug(
		"Writing header (%"PRIu32") + data (%"PRIu32") bytes downstream",
		NBD_REPLY_SIZE, proxy->rsp_buf_size - NBD_REPLY_SIZE
	);

	result = writeloop( proxy->downstream_fd, rsp_buf, proxy->rsp_buf_size );
	if ( result == -1 ) {
		debug( "Failed to send reply downstream" );
		return 0;
	}

	debug( "Reply sent" );
	return 1;
}


/* Here, we negotiate an NBD session with downstream, based on the information
 * we got on first connection to upstream. Then we wait for a request to come
 * in from downstream, read it into memory, then send it to upstream. If
 * upstream dies before responding, we reconnect to upstream and resend it.
 * Once we've got a response, we write it directly to downstream, and wait for a
 * new request. When downstream disconnects, or we receive an exit signal (which
 * can be blocked, unfortunately), we are finished.
 *
 * This is the simplest possible nbd proxy I can think of. It may not be at all
 * performant - let's see.
 */

void proxy_session( struct proxier* proxy )
{
    int downstream_fd = proxy->downstream_fd;
	uint64_t req_count = 0;
	int result;

	info( "Beginning proxy session on fd %i", downstream_fd );

	if ( !socket_nbd_write_hello( downstream_fd, proxy->upstream_size ) ) {
		debug( "Sending hello failed on fd %i, ending session", downstream_fd );
		return;
	}

	while( proxy_get_request_from_downstream( proxy ) ) {

		/* Don't start running the request if exit has been signalled */
		if ( proxy_should_exit( proxy, NULL, 0 ) ) {
			break;
		}

		do {
			result = proxy_run_request_upstream( proxy );
		} while ( result == 0 );

		/* We have to exit, but don't know if the request was successfully
		 * proxied or not. We could add that knowledge, and attempt to send a
		 * reply downstream if it was, but I don't think it's worth it.
		 */
		if ( result == -1 ) {
			break;
		}

		if ( !proxy_send_reply_downstream( proxy ) ) {
			break;
		}

		proxy->req_buf_size = 0;
		proxy->rsp_buf_size = 0;

		req_count++;
	};

	info(
		"Finished proxy session on fd %i after %"PRIu64" successful request(s)",
		downstream_fd, req_count
	);

	return;
}

/** Accept an NBD socket connection, dispatch appropriately */
int proxy_accept( struct proxier* params )
{
	NULLCHECK( params );

	int              client_fd;
	int              signal_fd = flexnbd_signal_fd( params->flexnbd );
	fd_set           fds;
	int              should_continue = 1;

	union mysockaddr client_address;
	socklen_t        socklen = sizeof( client_address );

	debug("accept loop starting");

	FD_ZERO(&fds);
	FD_SET(params->listen_fd, &fds);
	FD_SET(signal_fd, &fds);

	FATAL_IF_NEGATIVE(
		sock_try_select(FD_SETSIZE, &fds, NULL, NULL, NULL),
		SHOW_ERRNO( "select() failed" )
	);

	if ( proxy_should_exit( params, &fds, 0) ) {
		should_continue = 0;
	}

	if ( should_continue && FD_ISSET( params->listen_fd, &fds ) ) {
		client_fd = accept( params->listen_fd, &client_address.generic, &socklen );

		if ( sock_set_tcp_nodelay(client_fd, 1) == -1 ) {
			warn( SHOW_ERRNO( "Failed to set TCP_NODELAY" ) );
		}

		info( "Accepted nbd client socket fd %d", client_fd );
		params->downstream_fd = client_fd;
		proxy_session( params );

		if ( close( params->downstream_fd ) == -1 )  {
			warn( SHOW_ERRNO( "FIXME: close returned" ) );
		}

		params->downstream_fd = -1;
	}

	return should_continue;
}


void proxy_accept_loop( struct proxier* params )
{
	NULLCHECK( params );
	while( proxy_accept( params ) );
}

/** Closes sockets, frees memory and waits for all requests to clear */
void proxy_cleanup( struct proxier* params )
{
	NULLCHECK( params );

	info( "cleaning up" );

	if ( -1 != params->listen_fd ) {
		close( params->listen_fd );
	}

	debug( "Cleanup done" );
}

/** Full lifecycle of the proxier */
int do_proxy( struct proxier* params )
{
	NULLCHECK( params );

	error_set_handler( (cleanup_handler*) proxy_cleanup, params );

	debug( "Ensuring upstream server is open" );

	if ( !proxy_connect_to_upstream( params ) ) {
		info( "Couldn't connect to upstream server during initialization" );
		proxy_cleanup( params );
		return 1;
	};

	proxy_open_listen_socket( params );
	proxy_accept_loop( params );
	proxy_cleanup( params );

	return 0;
}
