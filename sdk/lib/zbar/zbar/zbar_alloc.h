/*------------------------------------------------------------------------
 *  Global memory allocator abstraction for bare-metal/embedded use.
 *
 *  All dynamic allocations inside the QR decoder route through the
 *  function pointers below.  The default implementation wraps the C
 *  library malloc/calloc/realloc/free/strdup.  On embedded targets,
 *  call zbar_mem_init() once with your own memory-pool backed
 *  allocators before decoding any image.
 *------------------------------------------------------------------------*/
#ifndef _ZBAR_ALLOC_H_
#define _ZBAR_ALLOC_H_

#include <stddef.h>

typedef void *(*zbar_malloc_fn)(size_t);
typedef void *(*zbar_calloc_fn)(size_t, size_t);
typedef void *(*zbar_realloc_fn)(void *, size_t);
typedef void  (*zbar_free_fn)(void *);
typedef char *(*zbar_strdup_fn)(const char *);

/** install custom memory allocators.
 * any of the pointers may be NULL, in which case the current (or
 * default libc) implementation is kept.
 * must be called before any decoding; never frees or allocates itself.
 */
void zbar_mem_init(zbar_malloc_fn malloc_fn,
                   zbar_calloc_fn calloc_fn,
                   zbar_realloc_fn realloc_fn,
                   zbar_free_fn free_fn,
                   zbar_strdup_fn strdup_fn);

void *zbar_malloc(size_t size);
void *zbar_calloc(size_t count, size_t size);
void *zbar_realloc(void *ptr, size_t size);
void  zbar_free(void *ptr);
char *zbar_strdup(const char *str);

#endif
