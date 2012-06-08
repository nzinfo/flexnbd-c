#include "serve.h"
#include "client.h"
#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "bitset.h"
#include "control.h"
#include "self_pipe.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

static inline void* sockaddr_address_data(struct sockaddr* sockaddr)
{
	NULLCHECK( sockaddr );

	struct sockaddr_in*  in  = (struct sockaddr_in*) sockaddr;
	struct sockaddr_in6* in6 = (struct sockaddr_in6*) sockaddr;
	
	if (sockaddr->sa_family == AF_INET)
		return &in->sin_addr;
	if (sockaddr->sa_family == AF_INET6)
		return &in6->sin6_addr;
	return NULL;
}

void server_dirty(struct server *serve, off64_t from, int len)
{
	NULLCHECK( serve );

	if (serve->mirror)
		bitset_set_range(serve->mirror->dirty_map, from, len);
}

#define SERVER_LOCK( s, f, msg ) \
	{ NULLCHECK( s ); \
	 SERVER_ERROR_ON_FAILURE( pthread_mutex_lock( &s->f ), msg ); }
#define SERVER_UNLOCK( s, f, msg ) \
	{ NULLCHECK( s ); \
	 SERVER_ERROR_ON_FAILURE( pthread_mutex_unlock( &s->f ), msg ); }

void server_lock_io( struct server * serve)
{
	SERVER_LOCK( serve, l_io, "Problem with I/O lock" );
}

void server_unlock_io( struct server* serve )
{
	SERVER_UNLOCK( serve, l_io, "Problem with I/O unlock" );
}

void server_lock_acl( struct server *serve )
{
	SERVER_LOCK( serve, l_acl, "Problem with ACL lock" );
}

void server_unlock_acl( struct server *serve )
{
	SERVER_UNLOCK( serve, l_acl, "Problem with ACL unlock" );
}


/** Prepares a listening socket for the NBD server, binding etc. */
void serve_open_server_socket(struct server* params)
{
	NULLCHECK( params );

	int optval=1;
	
	params->server_fd= socket(params->bind_to.generic.sa_family == AF_INET ? 
	  PF_INET : PF_INET6, SOCK_STREAM, 0);
	
	SERVER_ERROR_ON_FAILURE(params->server_fd, 
	  "Couldn't create server socket");

	SERVER_ERROR_ON_FAILURE(
		setsockopt(params->server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)),
		"Couldn't set SO_REUSEADDR"
	);

	SERVER_ERROR_ON_FAILURE(
		setsockopt(params->server_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)),
		"Couldn't set TCP_NODELAY"
	);

	SERVER_ERROR_ON_FAILURE(
		bind(params->server_fd, &params->bind_to.generic,
		  sizeof(params->bind_to)),
		"Couldn't bind server to IP address"
	);
	
	SERVER_ERROR_ON_FAILURE(
		listen(params->server_fd, params->tcp_backlog),
		"Couldn't listen on server socket"
	);
}

int tryjoin_client_thread( struct client_tbl_entry *entry, int (*joinfunc)(pthread_t, void **) )
{

	NULLCHECK( entry );
	NULLCHECK( joinfunc );

	int was_closed = 0;
	void * status;

	if (entry->thread != 0) {
		char s_client_address[64];

		memset(s_client_address, 0, 64);
		strcpy(s_client_address, "???");
		inet_ntop( entry->address.generic.sa_family, 
				sockaddr_address_data(&entry->address.generic), 
				s_client_address, 
				64 );

		if (joinfunc(entry->thread, &status) != 0) {
			if (errno != EBUSY)
				SERVER_ERROR_ON_FAILURE(-1, "Problem with joining thread");
		}
		else {
			debug("nbd thread %p exited (%s) with status %ld", 
					(int) entry->thread, 
					s_client_address, 
					(uint64_t)status);
			client_destroy( entry->client );
			entry->thread = 0;
			was_closed  = 1;
		}
	}

	return was_closed;
}


/**
 * Check to see if a client thread has finished, and if so, tidy up
 * after it.
 * Returns 1 if the thread was cleaned up and the slot freed, 0
 * otherwise.
 *
 * It's important that client_destroy gets called in the same thread
 * which signals the client threads to stop.  This avoids the
 * possibility of sending a stop signal via a signal which has already
 * been destroyed.  However, it means that stopped client threads,
 * including their signal pipes, won't be cleaned up until the next new
 * client connection attempt.
 */
int cleanup_client_thread( struct client_tbl_entry * entry )
{
	return tryjoin_client_thread( entry, pthread_tryjoin_np );
}


/**
 * Join a client thread after having sent a stop signal to it.
 * This function will not return until pthread_join has returned, so
 * ensures that the client thread is dead.
 */
int join_client_thread( struct client_tbl_entry *entry )
{
	return tryjoin_client_thread( entry, pthread_join );
}

/** We can only accommodate MAX_NBD_CLIENTS connections at once.  This function
 *  goes through the current list, waits for any threads that have finished
 *  and returns the next slot free (or -1 if there are none).
 */
int cleanup_and_find_client_slot(struct server* params)
{
	NULLCHECK( params );

	int slot=-1, i,j;

	for ( i = 0; i < MAX_NBD_CLIENTS; i++ ) {
		cleanup_client_thread( &params->nbd_client[i] );
	}

	for ( j = 0; j < MAX_NBD_CLIENTS; j++ ) {
		if( params->nbd_client[j].thread == 0 && slot == -1 ){
			slot = j;
			break;
		}
	}
	
	if ( -1 == slot ) { debug( "No client slot found." ); }

	return slot;
}


/** Check whether the address client_address is allowed or not according
 * to the current acl.  If params->acl is NULL, the result will be 1,
 * otherwise it will be the result of acl_includes().
 */
int server_acl_accepts( struct server *params, union mysockaddr * client_address )
{
	NULLCHECK( params );
	NULLCHECK( client_address );

	struct acl * acl;
	int accepted;

	server_lock_acl( params );
	{
		acl = params->acl;
		accepted = acl ? acl_includes( acl, client_address ) : 1;
	}
	server_unlock_acl( params );

	return accepted;
}


int server_should_accept_client( 
		struct server * params, 
		int client_fd, 
		union mysockaddr * client_address,
		char *s_client_address,
		size_t s_client_address_len )
{
	NULLCHECK( params );
	NULLCHECK( client_address );
	NULLCHECK( s_client_address );

	if (inet_ntop(client_address->generic.sa_family,
				sockaddr_address_data(&client_address->generic),
				s_client_address, s_client_address_len ) == NULL) {
		debug( "Rejecting client %s: Bad client_address", s_client_address );
		write(client_fd, "Bad client_address", 18);
		return 0;
	}

	if ( !server_acl_accepts( params, client_address ) ) {
		debug( "Rejecting client %s: Access control error", s_client_address );
		debug( "We %s have an acl, and default_deny is %s", 
				(params->acl ? "do" : "do not"),
				(params->default_deny ? "true" : "false") );
 		write(client_fd, "Access control error", 20);
 		return 0;
	}

	return 1;
}


/** Dispatch function for accepting an NBD connection and starting a thread
  * to handle it.  Rejects the connection if there is an ACL, and the far end's
  * address doesn't match, or if there are too many clients already connected.
  */
void accept_nbd_client(
		struct server* params, 
		int client_fd, 
		union mysockaddr* client_address)
{
	NULLCHECK(params);
	NULLCHECK(client_address);

	struct client* client_params;
	int slot;
	char s_client_address[64] = {0};


	if ( !server_should_accept_client( params, client_fd, client_address, s_client_address, 64 ) ) {
		close( client_fd );
		return;
	}

	slot = cleanup_and_find_client_slot(params); 
	if (slot < 0) {
		write(client_fd, "Too many clients", 16);
		close(client_fd);
		return;
	}
	
	debug( "Client %s accepted.", s_client_address );
	client_params = client_create( params, client_fd );

	params->nbd_client[slot].client = client_params;
	memcpy(&params->nbd_client[slot].address, client_address, 
	  sizeof(union mysockaddr));
	
	if (pthread_create(&params->nbd_client[slot].thread, NULL, client_serve, client_params) < 0) {
		debug( "Thread creation problem." );
		write(client_fd, "Thread creation problem", 23);
		client_destroy( client_params );
		close(client_fd);
		return;
	}
	
	debug("nbd thread %d started (%s)", (int) params->nbd_client[slot].thread, s_client_address);
}


int server_is_closed(struct server* serve)
{
	NULLCHECK( serve );
	return fd_is_closed( serve->server_fd );
}


void server_close_clients( struct server *params )
{
	NULLCHECK(params);

	int i, j;
	struct client_tbl_entry *entry;

	for( i = 0; i < MAX_NBD_CLIENTS; i++ ) {
		entry = &params->nbd_client[i];

		if ( entry->thread != 0 ) {
			client_signal_stop( entry->client );	
		}
	}
	for( j = 0; j < MAX_NBD_CLIENTS; j++ ) {
		join_client_thread( &params->nbd_client[i] );
	}
}


void server_replace_acl( struct server *serve, struct acl * new_acl )
{
	NULLCHECK(serve);
	NULLCHECK(new_acl);

	/* We need to lock around updates to the acl in case we try to
	 * destroy the old acl while checking against it.
	 */
	server_lock_acl( serve );
	{
		struct acl * old_acl = serve->acl;
		serve->acl = new_acl;
		/* We should always have an old_acl, but just in case... */
		if ( old_acl ) { acl_destroy( old_acl ); }
	}
	server_unlock_acl( serve );

	self_pipe_signal( serve->acl_updated_signal );
}


/** Accept either an NBD or control socket connection, dispatch appropriately */
void serve_accept_loop(struct server* params) 
{
	NULLCHECK( params );
	while (1) {
		int              activity_fd, client_fd;
		union mysockaddr client_address;
		fd_set           fds;
		socklen_t        socklen=sizeof(client_address);
		
		FD_ZERO(&fds);
		FD_SET(params->server_fd, &fds);
		self_pipe_fd_set( params->close_signal, &fds );
		if (params->control_socket_name)
			FD_SET(params->control_fd, &fds);
		
		SERVER_ERROR_ON_FAILURE(select(FD_SETSIZE, &fds, 
		  NULL, NULL, NULL), "select() failed");
		
		if ( self_pipe_fd_isset( params->close_signal, &fds ) ){
			server_close_clients( params );
			return;
		}
		
		activity_fd = FD_ISSET(params->server_fd, &fds) ? params->server_fd: 
		  params->control_fd;
		client_fd = accept(activity_fd, &client_address.generic, &socklen);
		
		if (activity_fd == params->server_fd) {
			debug("Accepted nbd client socket");
			accept_nbd_client(params, client_fd, &client_address);
		}
		if (activity_fd == params->control_fd) {
			debug("Accepted control client socket"); 
			accept_control_connection(params, client_fd, &client_address);
		}
			
	}
}

/** Initialisation function that sets up the initial allocation map, i.e. so
  * we know which blocks of the file are allocated.
  */
void serve_init_allocation_map(struct server* params)
{
	NULLCHECK( params );

	int fd = open(params->filename, O_RDONLY);
	off64_t size;

	SERVER_ERROR_ON_FAILURE(fd, "Couldn't open %s", params->filename);
	size = lseek64(fd, 0, SEEK_END);
	params->size = size;
	SERVER_ERROR_ON_FAILURE(size, "Couldn't find size of %s", 
	  params->filename);
	params->allocation_map = 
		build_allocation_map(fd, size, block_allocation_resolution);
	close(fd);
}


/* Tell the server to close all the things. */
void serve_signal_close( struct server * serve )
{
	NULLCHECK( serve );
	self_pipe_signal( serve->close_signal );
}


/** Closes sockets, frees memory and waits for all client threads to finish */
void serve_cleanup(struct server* params)
{
	NULLCHECK( params );

	int i;
	
	close(params->server_fd);
	close(params->control_fd);
	if (params->acl)
		free(params->acl);
	//free(params->filename);
	if (params->control_socket_name)
		//free(params->control_socket_name);
	pthread_mutex_destroy(&params->l_io);
	if (params->proxy_fd);
		close(params->proxy_fd);

	self_pipe_destroy( params->close_signal );
	self_pipe_destroy( params->acl_updated_signal );

	free(params->allocation_map);
	
	if (params->mirror)
		debug("mirror thread running! this should not happen!");
	
	for (i=0; i < MAX_NBD_CLIENTS; i++) {
		void* status;
		
		if (params->nbd_client[i].thread != 0) {
			debug("joining thread %d", i);
			pthread_join(params->nbd_client[i].thread, &status);
		}
	}
}

/** Full lifecycle of the server */
void do_serve(struct server* params)
{
	NULLCHECK( params );

	pthread_mutex_init(&params->l_io, NULL);
	pthread_mutex_init(&params->l_acl, NULL);

	params->close_signal = self_pipe_create();
	NULLCHECK( params->close_signal );
	params->acl_updated_signal = self_pipe_create();
	NULLCHECK( params->acl_updated_signal );
	
	serve_open_server_socket(params);
	serve_open_control_socket(params);
	serve_init_allocation_map(params);
	serve_accept_loop(params);
	serve_cleanup(params);
}

