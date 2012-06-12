#ifndef CLIENT_H
#define CLIENT_H


struct client {
	/* When we call pthread_join, if the thread is already dead
	 * we can get an ESRCH.  Since we have no other way to tell
	 * if that ESRCH is from a dead thread or a thread that never
	 * existed, we use a `stopped` flag to indicate a thread which
	 * did exist, but went away.  Only check this after a
	 * pthread_join call.
	 */
	int     stopped;
	int     socket;
	
	int     fileno;
	char*   mapped;

	struct self_pipe * stop_signal;
	
	struct server* serve; /* FIXME: remove above duplication */
};


void* client_serve(void* client_uncast);
struct client * client_create( struct server * serve, int socket );
void client_destroy( struct client * client );
void client_signal_stop( struct client * client );

#endif
