/*------------------------------------------------------------------------
 *  Global memory allocator abstraction implementation.
 *  See zbar_alloc.h for the public API.
 *------------------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>

#include "zbar_alloc.h"

/* statically initialized to the C library implementations */
static zbar_malloc_fn  _malloc  = malloc;
static zbar_calloc_fn  _calloc  = calloc;
static zbar_realloc_fn _realloc = realloc;
static zbar_free_fn    _free    = free;
static zbar_strdup_fn  zbar_mem_strdup  = NULL;   /* libc strdup is not C89 */

void zbar_mem_init (zbar_malloc_fn malloc_fn,
                    zbar_calloc_fn calloc_fn,
                    zbar_realloc_fn realloc_fn,
                    zbar_free_fn free_fn,
                    zbar_strdup_fn strdup_fn)
{
    if(malloc_fn)  _malloc  = malloc_fn;
    if(calloc_fn)  _calloc  = calloc_fn;
    if(realloc_fn) _realloc = realloc_fn;
    if(free_fn)    _free    = free_fn;
    if(strdup_fn)  zbar_mem_strdup  = strdup_fn;
}

void *zbar_malloc (size_t size)
{
    return(_malloc(size));
}

void *zbar_calloc (size_t count, size_t size)
{
    return(_calloc(count, size));
}

void *zbar_realloc (void *ptr, size_t size)
{
    return(_realloc(ptr, size));
}

void zbar_free (void *ptr)
{
    _free(ptr);
}

char *zbar_strdup (const char *str)
{
    if(!str)
        return(NULL);
    /* honor a custom strdup if one was registered */
    if(zbar_mem_strdup)
        return(zbar_mem_strdup(str));
    /* portable fallback: malloc + copy (avoids libc strdup which is
     * POSIX-only and may not exist in embedded toolchains) */
    size_t len = strlen(str) + 1;
    char *copy = (char *)_malloc(len);
    if(copy)
        memcpy(copy, str, len);
    return(copy);
}
