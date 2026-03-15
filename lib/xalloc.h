/**
 * @file xalloc.h
 * @brief Checked allocation utilities.
 *
 * Provides wrappers around malloc/calloc that abort on allocation
 * failure instead of returning NULL. Adapted from the 15-122
 * Principles of Imperative Computation course library.
 */

#include <stdio.h>

#ifndef _XALLOC_H_
#define _XALLOC_H_

/**
 * @brief Allocate a zero-initialized array, aborting on failure.
 *
 * Behaves like calloc() but calls exit(1) if the allocation fails
 * rather than returning NULL.
 *
 * @param nobj  Number of objects to allocate.
 * @param size  Size of each object in bytes.
 * @return Non-NULL pointer to the allocated and zeroed memory.
 */
void* xcalloc(size_t nobj, size_t size)
  /*@ensures \result != NULL; @*/ ;

/**
 * @brief Allocate uninitialized memory, aborting on failure.
 *
 * Behaves like malloc() but calls exit(1) if the allocation fails
 * rather than returning NULL.
 *
 * @param size  Number of bytes to allocate.
 * @return Non-NULL pointer to the allocated memory.
 */
void* xmalloc(size_t size)
  /*@ensures \result != NULL; @*/ ;

#endif
