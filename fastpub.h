#ifndef __FASTPUB_H
#define __FASTPUB_H

/*
 * FastPub - Zero-Copy Non-Blocking Shared Memory Publisher/Subscriber IPC Library
 */

#include <stdbool.h>
#include <stddef.h>

struct fastpub {
    void *info;
	int shmfd;
	size_t shm_size;
	size_t buffer_size;
	size_t max_subscribers;
	bool publisher;
	char *name;
};

/*
 * Publisher functions
 *
 * struct fastpub *fp = fastpub_pubopen("foo", 1024, 1);
 * while (1) {
 *   char *buffer = fastpub_nextbuf(fp); // buffer has 1024 bytes
 *   ...
 *   fastpub_publish(fp);
 * }
 * fastpub_close(fp);
 *
 * fastpub_init("foo", 1024, 1);
 */
struct fastpub *fastpub_pubopen(const char *name, size_t buffer_size, size_t max_subscribers);
void *fastpub_nextbuf(struct fastpub *);
void  fastpub_publish(struct fastpub *);

/*
 * Subscriber functions
 *
 * struct fastpub *fp = fastpub_subopen("foo");
 * while (1) {
 *   char *buffer = fastpub_current(fp); // buffer has fp->buffer_size bytes
 *   ...
 *   fastpub_release(fp, buffer);
 * }
 * fastpub_close(fp);
 */
struct fastpub *fastpub_subopen(const char *name);
void *fastpub_next_update(struct fastpub *);
void *fastpub_current(struct fastpub *);
void  fastpub_release(struct fastpub *, void *buffer);

void fastpub_close(struct fastpub *);

#endif//__FASTPUB_H
