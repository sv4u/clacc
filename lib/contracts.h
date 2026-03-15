/**
 * @file contracts.h
 * @brief Debug contract macros simulating cc0 -d behavior.
 *
 * Provides ASSERT, REQUIRES, and ENSURES macros that expand to
 * assert() when compiled with @c -DDEBUG, or to no-ops otherwise.
 * Adapted from the 15-122 Principles of Imperative Computation course.
 *
 * @note This header may be included multiple times (with and without
 *       DEBUG defined) because it undefines and redefines its macros
 *       on each inclusion.
 */

#include <assert.h>

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef REQUIRES
#undef REQUIRES
#endif

#ifdef ENSURES
#undef ENSURES
#endif

#ifdef DEBUG

/** @brief Assert a general invariant (active only in debug builds). */
#define ASSERT(COND) assert(COND)
/** @brief Assert a function precondition (active only in debug builds). */
#define REQUIRES(COND) assert(COND)
/** @brief Assert a function postcondition (active only in debug builds). */
#define ENSURES(COND) assert(COND)

#else

#define ASSERT(COND) ((void)0)
#define REQUIRES(COND) ((void)0)
#define ENSURES(COND) ((void)0)

#endif
