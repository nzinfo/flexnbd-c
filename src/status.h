#ifndef STATUS_H
#define STATUS_H


/* Status reports
 *
 * The status will be reported by writing to a file descriptor.  The
 * status report will be on a single line.  The status format will be:
 *
 *   A=B C=D
 *
 * That is, a space-separated list of label,value pairs, each pair
 * separated by an '=' character.  Neither ' ' nor '=' will appear in
 * either labels or values.
 *
 * Boolean values will appear as the strings "true" and "false".
 *
 * The following status fields are defined:
 *
 * pid:
 *      The current process ID.
 *
 * size:
 *  The size of the backing file being served, in bytes.
 *
 * has_control:
 * 	This will be false when the server is listening for an incoming
 * 	migration.  It will switch to true when the end-of-migration
 * 	handshake is successfully completed.
 * 	If the server is started in "serve" mode, this will never be
 * 	false.
 *
 * is_migrating:
 * 	This will be false when the server is started in either "listen"
 * 	or "serve" mode.  It will become true when a server in "serve"
 * 	mode starts a migration, and will become false again when the
 * 	migration terminates, successfully or not.
 *	If the server is currently in "listen" mode, this will never be
 *	true.
 *
 * If is_migrating is true, then a number of other attributes may appear,
 * relating to the progress of the migration.
 *
 * migration_duration:
 *   How long the migration has been running for, in ms.
 *
 * migration_speed:
 *   Network transfer speed, in bytes/second. This only takes dirty bytes
 *   into account.
 *
 * migration_pass:
 *  When migrating, we perform a number of passes over the file. This indicates
 *  the current pass.
 *
 * pass_dirty_bytes:
 *  For the current pass, how many dirty bytes have we found so far? These are
 *  classed as bytes that we are required to send to the destination.
 *
 * pass_clean_bytes:
 *  For the current pass, how many clean bytes? These are bytes we don't need
 *  to send to the destination. Once all the bytes are clean, the migration is
 *  done.
 *
 */


#include "serve.h"

#include <sys/types.h>
#include <unistd.h>

struct status {
	pid_t pid;
	uint64_t size;
	int has_control;
	int is_mirroring;
	int migration_pass;
	uint64_t pass_dirty_bytes;
	uint64_t pass_clean_bytes;

	uint64_t migration_duration;
	uint64_t migration_speed;
	uint64_t migration_speed_limit;
};

/** Create a status object for the given server. */
struct status * status_create( struct server * );

/** Output the given status object to the given file descriptot */
int status_write( struct status *, int fd );

/** Free the status object */
void status_destroy( struct status * );



#endif

