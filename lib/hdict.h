/**
 * @file hdict.h
 * @brief Hash dictionary interface.
 *
 * Generic hash table mapping opaque keys to opaque values, adapted from
 * the 15-122 Principles of Imperative Computation course library.
 * Used by the clacc parser for function name lookup.
 */

#include <stdlib.h>
#include <stdbool.h>

#ifndef _HDICT_H_
#define _HDICT_H_

/** @brief Opaque key type (typically @c char* for clacc). */
typedef void *hdict_key;

/** @brief Opaque value type (typically @c uint16_t* for clacc). */
typedef void *hdict_value;

/** @brief Equality predicate for keys. */
typedef bool key_equal_fn(hdict_key x, hdict_key y);

/** @brief Hash function for keys. */
typedef size_t key_hash_fn(hdict_key x);

/** @brief Destructor for values; called when an entry is evicted. */
typedef void value_free_fn(hdict_value x);

/** @brief Opaque handle to a hash dictionary instance. */
typedef struct hdict_header* hdict_t;

/**
 * @brief Create a new hash dictionary.
 *
 * @param capacity    Initial number of hash buckets (must be > 0).
 * @param key_equal   Equality predicate for keys (must not be NULL).
 * @param key_hash    Hash function for keys (must not be NULL).
 * @param value_free  Optional destructor for values; if NULL, evicted
 *                    values are not freed.
 * @return Non-NULL handle to the new dictionary.
 */
hdict_t hdict_new(size_t capacity,
                  key_equal_fn *key_equal,
                  key_hash_fn *key_hash,
                  value_free_fn *value_free)
  /*@requires capacity > 0; @*/ 
  /*@requires key_equal != NULL && key_hash != NULL; @*/
  /* if value_free is NULL, then elements will not be freed */
  /*@ensures \result != NULL; @*/ ;

/**
 * @brief Insert a key-value pair into the dictionary.
 *
 * If the key already exists, the old value is replaced and returned so
 * the caller can free it if necessary.
 *
 * @param H  Dictionary handle (must not be NULL).
 * @param k  Key to insert.
 * @param v  Value to associate with the key.
 * @return The previously stored value for @p k, or NULL if the key was
 *         not present.
 */
hdict_value hdict_insert(hdict_t H, hdict_key k, hdict_value v)
  /*@requires H != NULL; @*/ ;

/**
 * @brief Look up a key in the dictionary.
 *
 * @param H  Dictionary handle (must not be NULL).
 * @param x  Key to search for.
 * @return The associated value, or NULL if the key is not found.
 */
hdict_value hdict_lookup(hdict_t H, hdict_key x)
  /*@requires H != NULL; @*/ ;

/**
 * @brief Free the dictionary and all stored values.
 *
 * Calls the @c value_free function (if provided at creation) on every
 * stored value before releasing the dictionary itself.
 *
 * @param H  Dictionary handle (must not be NULL).
 */
void hdict_free(hdict_t H)
  /*@requires H != NULL; @*/ ;

#endif
