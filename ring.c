// mah: requirements:
// this _must_ run on i386, x86_64, arm UP and SMP
// it seems [3] does all we need

// comparison references:
// [1] https://subversion.assembla.com/svn/portaudio/portaudio/trunk/src/common/pa_ringbuffer.h
// [2] https://subversion.assembla.com/svn/portaudio/portaudio/trunk/src/common/pa_ringbuffer.c
// [3] https://subversion.assembla.com/svn/portaudio/portaudio/trunk/src/common/pa_memorybarrier.h
// [4]: https://github.com/jackaudio/jack2/blob/master/common/ringbuffer.c
// [5]: http://julien.benoist.name/lockfree.html
// [6]: http://julien.benoist.name/lockfree/lockfree.tar.bz2/atomic-queue/lfq.c

// general comment: I'm very wary how the atomic ops in [6] relate to this - see
// the CAS,DWCAS ops there?


#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/uio.h>

#include "ring.h"


/// Round up X to closest 2^n
static ring_size_t size_aligned(ring_size_t x)
{
    // TODO: Replace with unconditional one

    // mah: what is a 'unconditional one'?
    // mah: the alignment is dubious (any portable way to do this?)
    
    static const int align = 0x11;
    if (x & align)
	return x + align + 1 - (x & align);
    return x;
}

int ring_init(ringbuffer_t *ring, size_t size, void * memory)
{
    if (!memory) {
	ring->header = malloc(sizeof(ring_header_t) + size);
	if (ring->header == NULL)
	    return ENOMEM;
	ring->header->size = size;
	ring->header->head = ring->header->tail = 0;
    } else
	ring->header = memory;
    ring->buf = (char *) (ring->header + 1);
    return 0;
}

inline ring_size_t * _size_at(ringbuffer_t *ring, size_t off)
{
    return (ring_size_t *) (ring->buf + off);
}

int ring_write(ringbuffer_t *ring, void * data, size_t size)
{
    size_t a = size_aligned(size + sizeof(ring_size_t));
    if (a > ring->header->size)
	return ERANGE;
    if ((ring->header->tail + a) % ring->header->size == ring->header->head)
	return EAGAIN;
    if (ring->header->tail + size > ring->header->size) {
	if (ring->header->head < a)
	    return EAGAIN;
	// Wrap
	*_size_at(ring, ring->header->tail) = -1;
	// mah: see [2]:144
	// PaUtil_WriteMemoryBarrier(); ???
	ring->header->tail = 0;
    }
    *_size_at(ring, ring->header->tail) = size;
    memmove(ring->buf + ring->header->tail + sizeof(ring_size_t), data, size);
    // mah: see [2]:144
    // PaUtil_WriteMemoryBarrier(); ???

    // mah: see [6]:69
    // should this be CAS(&(ring->header->tail, ring->header->tail, ring->header->tail+a) ?
    ring->header->tail += a;
    return 0;
}

void * ring_next(ringbuffer_t *ring)
{
    if (ring->header->head == ring->header->tail)
	return 0;
    // mah: see [2]:181
    // PaUtil_ReadMemoryBarrier(); ???
    return ring->buf + ring->header->head + sizeof(ring_size_t);
}

ring_size_t ring_next_size(ringbuffer_t *ring)
{
    if (ring->header->head == ring->header->tail)
	return -1;
    return *_size_at(ring, ring->header->head);
}

struct iovec ring_next_iovec(ringbuffer_t *ring)
{
    ring_size_t size = ring_next_size(ring);
    if (size < 0) {
	static const struct iovec iov = { .iov_len = 0 };
	return iov;
    }
    struct iovec iov = { .iov_len = size, .iov_base = ring_next(ring) };
    return iov;
}

void ring_shift(ringbuffer_t *ring)
{
    if (ring->header->head == ring->header->tail)
	return;
    // mah: [2]:192 
    // PaUtil_FullMemoryBarrier(); ???
    ring_size_t size = *_size_at(ring, ring->header->head);
    if (size < 0) {
	ring->header->head = 0;
	return;
    }
    size = size_aligned(size + sizeof(ring_size_t));
    ring->header->head = (ring->header->head + size) % ring->header->size;
}

void ring_dump(ringbuffer_t *ring, const char *name)
{
    if (ring_next_size(ring) < 0) {
	printf("Ring %s is empty\n", name);
	return;
    }
    printf("Data in %s: %d %.*s\n", name,
	   ring_next_size(ring), ring_next_size(ring), (char *) ring_next(ring));
}

#if 0
int main()
{
    ringbuffer_t ring1, ring2;
    ringbuffer_t *ro = &ring1, *rw = &ring2;
    ring_init(ro, 1024, 0);
    ring_init(rw, 0, ro->header);

    ring_dump(ro, "ro"); ring_dump(rw, "rw");

    ring_write(rw, "test", 4);
    ring_dump(ro, "ro"); ring_dump(rw, "rw");

    ring_write(rw, "test", 0);
    ring_dump(ro, "ro"); ring_dump(rw, "rw");

    ring_shift(ro);
    ring_dump(ro, "ro"); ring_dump(rw, "rw");

    ring_shift(ro);
    ring_dump(ro, "ro"); ring_dump(rw, "rw");
}



Ring ro is empty
Ring rw is empty

Data in ro: 4 test
Data in rw: 4 test

Data in ro: 4 test
Data in rw: 4 test

Data in ro: 0 
Data in rw: 0 

Ring ro is empty
Ring rw is empty

#endif
