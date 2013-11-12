#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include "fastpub.h"

struct fastpub_info {
    uint32_t buffer_size;
    uint32_t buffer_count;

    uint32_t ready;
    uint32_t next; // struct fastpub_buffer *
    uint32_t current; // struct fastpub_buffer *
    uint32_t slack; // struct fastpub_buffer *
    
    pthread_mutex_t mutex;
    pthread_mutexattr_t mutex_attr;

    pthread_cond_t update_cond;
    pthread_condattr_t update_cond_attr;
} __attribute__((aligned(64)));

struct fastpub_buffer {
    uint32_t refcount;
    uint32_t next; // struct fastpub_buffer *
} __attribute__((aligned(64)));

struct fastpub *fastpub_pubopen(const char *name, size_t buffer_size, size_t max_subscribers) {

    // allocate descriptor structure
    struct fastpub *self = malloc(sizeof(struct fastpub));
    if (!self) {
        return NULL;
    }

    self->buffer_size = buffer_size;
    self->max_subscribers = max_subscribers;
    self->publisher = true;
    self->name = strdup(name);

    // open shared memory file
    int shmfd = shm_open(name, O_RDWR | O_CREAT, 0660);
    self->shmfd = shmfd;
    if (shmfd < 0) {
        free(self->name);
        free(self);
        return NULL;
    }

    // resize shared memory file
    self->shm_size = sizeof(struct fastpub_info) + 
        (sizeof(struct fastpub_buffer) + buffer_size) * (max_subscribers + 2);
    if (ftruncate(shmfd, self->shm_size)) {
        free(self->name);
        free(self);
        shm_unlink(name);
        return NULL;
    }

    // map shared memory file
    struct fastpub_info *info = mmap(NULL, self->shm_size, 
        PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    self->info = info;
    if (!self->info) {
        free(self->name);
        free(self);
        shm_unlink(name);
        return NULL;
    }

    // create memory layout
    // the first block is next, and remaining ones are put in slack
    info->ready = 0;
    info->buffer_size = buffer_size;
    info->buffer_count = max_subscribers + 2;
    info->next = sizeof(struct fastpub_info);
    info->slack = sizeof(struct fastpub_info) + sizeof(struct fastpub_buffer) + buffer_size;
    for (size_t i = 0; i < max_subscribers + 1; i++) {
        struct fastpub_buffer *buffer = (void*) 
            (info->slack + (uintptr_t) info + 
            (sizeof(struct fastpub_buffer) + buffer_size) * i);
        buffer->refcount = 0;
        if (i != max_subscribers) {
            buffer->next = info->slack + 
                (sizeof(struct fastpub_buffer) + buffer_size) * (i + 1);
        }
        else {
            buffer->next = 0;
        }
    }

    // initialize mutex
    {
        pthread_mutexattr_init(&info->mutex_attr);
        pthread_mutexattr_setpshared(&info->mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&info->mutex, &info->mutex_attr);

        pthread_condattr_init(&info->update_cond_attr);
        pthread_condattr_setpshared(&info->update_cond_attr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&info->update_cond, &info->update_cond_attr);

        info->ready = 0x40404040;
    }

    return self;
}

struct fastpub *fastpub_subopen(const char *name) {

    // allocate descriptor structure
    struct fastpub *self = malloc(sizeof(struct fastpub));
    if (!self) {
        return NULL;
    }

    retry:

    self->publisher = false;
    self->name = strdup(name);

    // open shared memory file
    int shmfd = shm_open(name, O_RDWR | O_CREAT, 0660);
    self->shmfd = shmfd;
    if (shmfd < 0) {
        free(self->name);
        free(self);
        return NULL;
    }

    // resize shared memory file
    self->shm_size = lseek(shmfd, 0, SEEK_END) + 1;

    if (self->shm_size == 0) {
        close(shmfd);
        usleep(10000);
        goto retry;
    }

    // map shared memory file
    struct fastpub_info *info = mmap(NULL, self->shm_size, 
        PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    self->info = info;

    if (!self->info) {
        free(self->name);
        free(self);
        shm_unlink(name);
        return NULL;
    }

    while (info->ready != 0x40404040) usleep(10000);

    return self;
}

void *fastpub_nextbuf(struct fastpub *self) {
    struct fastpub_info *info = self->info;
    return (void*) ((uintptr_t) info + info->next + sizeof(struct fastpub_buffer));
}

static void _push_slack(struct fastpub *self, uint32_t offset) {
    struct fastpub_info *info = self->info;

    ((struct fastpub_buffer*) ((uintptr_t) info + offset))->next = info->slack;
    info->slack = offset;
}

static uint32_t _pop_slack(struct fastpub *self) {
    struct fastpub_info *info = self->info;

    struct fastpub_buffer *block = (void*) ((uintptr_t) info + info->slack);
    uint32_t block_offset = info->slack;
    info->slack = block->next;
    return block_offset;
}

void fastpub_publish(struct fastpub *self) {
    struct fastpub_info *info = self->info;
    
    pthread_mutex_lock(&info->mutex);
    struct fastpub_buffer *current = (void*) ((uintptr_t) info + info->current);
    struct fastpub_buffer *next = (void*) ((uintptr_t) info + info->next);

    // if --current.refcount == 0, push_slack(current)
    if (--current->refcount == 0) {
        _push_slack(self, info->current);
    }

    // current <- next
    next->refcount = 1;
    info->current = info->next;

    // next <- pop_slack()
    info->next = _pop_slack(self);
    pthread_mutex_unlock(&info->mutex);

    pthread_cond_broadcast(&info->update_cond);
}

void *fastpub_current(struct fastpub *self) {
    struct fastpub_info *info = self->info;

    pthread_mutex_lock(&info->mutex);
    struct fastpub_buffer *current = (void*) ((uintptr_t) info + info->current);
    current->refcount++;
    pthread_mutex_unlock(&info->mutex);

    return (void*) ((uintptr_t) current + sizeof(struct fastpub_buffer));
}

void fastpub_release(struct fastpub *self, void *buffer) {
    struct fastpub_info *info = self->info;

    pthread_mutex_lock(&info->mutex);
    struct fastpub_buffer *fpbuffer = (void*) ((intptr_t) buffer - sizeof(struct fastpub_buffer));
    if (--fpbuffer->refcount == 0) {
        _push_slack(self, (uint32_t) ((uintptr_t) fpbuffer - (uintptr_t) self->info));
    }
    pthread_mutex_unlock(&info->mutex);
}

void *fastpub_next_update(struct fastpub *self) {
    struct fastpub_info *info = self->info;

    pthread_mutex_lock(&info->mutex);
    pthread_cond_wait(&info->update_cond, &info->mutex);
    struct fastpub_buffer *current = (void*) ((uintptr_t) info + info->current);
    current->refcount++;
    pthread_mutex_unlock(&info->mutex);

    return (void*) ((uintptr_t) current + sizeof(struct fastpub_buffer));
}

void fastpub_close(struct fastpub *self) {

//    if (self->publisher) {
//        shm_unlink(self->name);
//    }
    
    munmap(self->info, self->shm_size);
    close(self->shmfd);
    free(self->name);
    free(self);
}
