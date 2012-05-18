#include "params.h"
#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "bitset.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const int block_allocation_resolution = 4096;//128<<10;

/**
 * So waiting on client->socket is len bytes of data, and we must write it all
 * to client->mapped.  However while doing do we must consult the bitmap
 * client->block_allocation_map, which is a bitmap where one bit represents
 * block_allocation_resolution bytes.  Where a bit isn't set, there are no 
 * disc blocks allocated for that portion of the file, and we'd like to keep
 * it that way.  
 *
 * If the bitmap shows that every block in our prospective write is already
 * allocated, we can proceed as normal and make one call to writeloop.  
 * 
 */
void write_not_zeroes(struct client_params* client, off64_t from, int len)
{
	char *map = client->block_allocation_map;

	while (len > 0) {
		/* so we have to calculate how much of our input to consider
		 * next based on the bitmap of allocated blocks.  This will be
		 * at a coarser resolution than the actual write, which may
		 * not fall on a block boundary at either end.  So we look up
		 * how many blocks our write covers, then cut off the start
		 * and end to get the exact number of bytes.
		 */
		int first_bit = from/block_allocation_resolution;
		int last_bit  = (from+len+block_allocation_resolution-1) / 
		  block_allocation_resolution;
		int run = bit_run_count(map, first_bit, last_bit-first_bit) *
		  block_allocation_resolution;
		
		debug("write_not_zeroes: %ld+%d, first_bit=%d, last_bit=%d, run=%d", 
		  from, len, first_bit, last_bit, run);

		run -= from % block_allocation_resolution; /*start*/
		
		if (first_bit == last_bit)
			run -= block_allocation_resolution - 
			  ((from+len) % block_allocation_resolution); /*end*/
		
		debug("run adjusted to %d", run);
		
		#define DO_READ(dst, len) CLIENT_ERROR_ON_FAILURE( \
			readloop( \
				client->socket, \
				(dst), \
				(len) \
			), \
			"read failed %ld+%d", from, (len) \
		)
		
		if (bit_is_set(map, from/8)) {
			/*debug("writing the lot");*/
			/* already allocated, just write it all */
			DO_READ(client->mapped + from, run);
			len  -= run;
			from += run;
		}
		else {
			char zerobuffer[block_allocation_resolution];
			/* not allocated, read in block_allocation_resoution */
			while (run > 0) {
				char *dst = client->mapped+from;
				int bit = from/block_allocation_resolution;
				int blockrun = block_allocation_resolution - 
				  (from % block_allocation_resolution);
				if (blockrun > run)
					blockrun = run;
				
				/*debug("writing partial: bit=%d, blockrun=%d",
				  bit, blockrun);*/
				
				DO_READ(zerobuffer, blockrun);
				
				/* This reads the buffer twice in the worst case 
				 * but we're leaning on memcmp failing early
				 * and memcpy being fast, rather than try to
				 * hand-optimized something specific.
				 */
				if (zerobuffer[0] != 0 || 
				    memcmp(zerobuffer, zerobuffer + 1, blockrun)) {
					memcpy(dst, zerobuffer, blockrun);
					bit_set(map, bit);
					/*debug("non-zero, copied and set bit %d", bit);*/
					/* at this point we could choose to
					 * short-cut the rest of the write for
					 * faster I/O but by continuing to do it
					 * the slow way we preserve as much 
					 * sparseness as possible.
					 */
				}
				else {
					/*debug("all zero, skip write");*/
				}
				len  -= blockrun;
				run  -= blockrun;
				from += blockrun;
			}
		}
	}
}

int client_serve_request(struct client_params* client)
{
	off64_t               offset;
	struct nbd_request    request;
	struct nbd_reply      reply;
	
	if (readloop(client->socket, &request, sizeof(request)) == -1) {
		if (errno == 0) {
			debug("EOF reading request");
			return 1; /* neat point to close the socket */
		}
		else {
			CLIENT_ERROR_ON_FAILURE(-1, "Error reading request");
		}
	}
	
	reply.magic = htobe32(REPLY_MAGIC);
	reply.error = htobe32(0);
	memcpy(reply.handle, request.handle, 8);
	
	debug("request type %d", be32toh(request.type));
	
	if (be32toh(request.magic) != REQUEST_MAGIC)
		CLIENT_ERROR("Bad magic %08x", be32toh(request.magic));
		
	switch (be32toh(request.type))
	{
	case REQUEST_READ:
	case REQUEST_WRITE:
		/* check it's not out of range */
		if (be64toh(request.from) < 0 || 
		    be64toh(request.from)+be32toh(request.len) > client->size) {
			debug("request read %ld+%d out of range", 
			  be64toh(request.from), 
			  be32toh(request.len)
			);
			reply.error = htobe32(1);
			write(client->socket, &reply, sizeof(reply));
			return 0;
		}
		break;
		
	case REQUEST_DISCONNECT:
		debug("request disconnect");
		return 1;
		
	default:
		CLIENT_ERROR("Unknown request %08x", be32toh(request.type));
	}
	
	switch (be32toh(request.type))
	{
	case REQUEST_READ:
		debug("request read %ld+%d", be64toh(request.from), be32toh(request.len));
		write(client->socket, &reply, sizeof(reply));
				
		offset = be64toh(request.from);
		CLIENT_ERROR_ON_FAILURE(
			sendfileloop(
				client->socket, 
				client->fileno, 
				&offset, 
				be32toh(request.len)
			),
			"sendfile failed from=%ld, len=%d",
			offset,
			be32toh(request.len)
		);
		break;
		
	case REQUEST_WRITE:
		debug("request write %ld+%d", be64toh(request.from), be32toh(request.len));
		if (client->block_allocation_map) {
			write_not_zeroes(
				client, 
				be64toh(request.from), 
				be32toh(request.len)
			);
		}
		else {
			CLIENT_ERROR_ON_FAILURE(
				readloop(
					client->socket,
					client->mapped + be64toh(request.from),
					be32toh(request.len)
				),
				"read failed from=%ld, len=%d",
				be64toh(request.from),
				be32toh(request.len)
			);
		}
		write(client->socket, &reply, sizeof(reply));
		
		break;
	}
	return 0;
}

void client_open_file(struct client_params* client)
{
	client->fileno = open(client->filename, O_RDWR|O_DIRECT|O_SYNC);
	CLIENT_ERROR_ON_FAILURE(client->fileno, "Couldn't open %s", 
	  client->filename);
	client->size = lseek64(client->fileno, 0, SEEK_END);
	CLIENT_ERROR_ON_FAILURE(client->fileno, "Couldn't seek to end of %s",
	  client->filename);
	client->mapped = mmap64(NULL, client->size, PROT_READ|PROT_WRITE, 
	  MAP_SHARED, client->fileno, 0);
	CLIENT_ERROR_ON_FAILURE((long) client->mapped, "Couldn't map file %s",
	  client->filename);
	debug("opened %s size %ld on fd %d @ %p", client->filename, client->size, client->fileno, client->mapped);
}

void client_send_hello(struct client_params* client)
{
	struct nbd_init init;
	
	memcpy(init.passwd, INIT_PASSWD, sizeof(INIT_PASSWD));
	init.magic = htobe64(INIT_MAGIC);
	init.size = htobe64(client->size);
	memset(init.reserved, 0, 128);
	CLIENT_ERROR_ON_FAILURE(
		writeloop(client->socket, &init, sizeof(init)),
		"Couldn't send hello"
	);
}

void* client_serve(void* client_uncast)
{
	struct client_params* client = (struct client_params*) client_uncast;
	
	client_open_file(client);
	client_send_hello(client);
	
	while (client_serve_request(client) == 0)
		;
		
	CLIENT_ERROR_ON_FAILURE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);
	
	free(client);
	return NULL;
}

static int testmasks[9] = { 0,128,192,224,240,248,252,254,255 };

int is_included_in_acl(int list_length, struct ip_and_mask** list, struct sockaddr* test)
{
	int i;
	
	for (i=0; i < list_length; i++) {
		struct ip_and_mask *entry = list[i];
		int testbits;
		char *raw_address1, *raw_address2;
		
		debug("checking acl entry %d", i);
		
		if (test->sa_family != entry->ip.family)
			continue;
		
		if (test->sa_family == AF_INET) {
			raw_address1 = (char*) 
			  &((struct sockaddr_in*) test)->sin_addr;
			raw_address2 = (char*) &entry->ip.v4.sin_addr;
		}
		else if (test->sa_family == AF_INET6) {
			raw_address1 = (char*) 
			  &((struct sockaddr_in6*) test)->sin6_addr;
			raw_address2 = (char*) &entry->ip.v6.sin6_addr;
		}
		
		for (testbits = entry->mask; testbits > 0; testbits -= 8) {
			debug("testbits=%d, c1=%d, c2=%d", testbits, raw_address1[0], raw_address2[0]);
			if (testbits >= 8) {
				if (raw_address1[0] != raw_address2[0])
					goto no_match;
			}
			else {
				if ((raw_address1[0] & testmasks[testbits%8]) !=
				    (raw_address2[0] & testmasks[testbits%8]) )
				    	goto no_match;
			}
			
			raw_address1++;
			raw_address2++;
		}
		
		return 1;
		
		no_match: ;
		debug("no match");
	}
	
	return 0;
}

void serve_open_socket(struct mode_serve_params* params)
{
	params->server = socket(PF_INET, SOCK_STREAM, 0);
	
	SERVER_ERROR_ON_FAILURE(params->server, 
	  "Couldn't create server socket");
	  
	SERVER_ERROR_ON_FAILURE(
		bind(params->server, &params->bind_to.generic, 
		  sizeof(params->bind_to.generic)),
		"Couldn't bind server to IP address"
	);
	
	SERVER_ERROR_ON_FAILURE(
		listen(params->server, params->tcp_backlog),
		"Couldn't listen on server socket"
	);
}

void serve_accept_loop(struct mode_serve_params* params) 
{
	while (1) {
		pthread_t client_thread;
		struct sockaddr client_address;
		struct client_params* client_params;
		socklen_t socket_length=0;
		
		int client_socket = accept(params->server, &client_address, 
		  &socket_length);
		  
		SERVER_ERROR_ON_FAILURE(client_socket, "accept() failed");
		
		if (params->acl && 
		    !is_included_in_acl(params->acl_entries, params->acl, &client_address)) {
			write(client_socket, "Access control error", 20);
			close(client_socket);
			continue;
		}
		
		client_params = xmalloc(sizeof(struct client_params));
		client_params->socket = client_socket;
		client_params->filename = params->filename;
		client_params->block_allocation_map = 
		  params->block_allocation_map;
		
		client_thread = pthread_create(&client_thread, NULL, 
		  client_serve, client_params);
		SERVER_ERROR_ON_FAILURE(client_thread,
		  "Failed to create client thread");
		/* FIXME: keep track of them? */
		/* FIXME: maybe shouldn't be fatal? */
	}
}

void serve_init_allocation_map(struct mode_serve_params* params)
{
	int fd = open(params->filename, O_RDONLY);
	off64_t size;
	SERVER_ERROR_ON_FAILURE(fd, "Couldn't open %s", params->filename);
	size = lseek64(fd, 0, SEEK_END);
	SERVER_ERROR_ON_FAILURE(size, "Couldn't find size of %s", 
	  params->filename);
	params->block_allocation_map = 
		build_allocation_map(fd, size, block_allocation_resolution);
	close(fd);
}

void do_serve(struct mode_serve_params* params)
{
	serve_open_socket(params);
	serve_init_allocation_map(params);
	serve_accept_loop(params);
}
