/**
 * @file c0vm_abort.h
 * @brief C0 VM abnormal termination handlers.
 *
 * Functions for reporting runtime errors from the C0 virtual machine.
 * Each prints a diagnostic to stderr and calls exit(). Adapted from
 * the 15-122 Principles of Imperative Computation course.
 *
 * @author Rob Simmons
 */

#ifndef _C0VM_ABORT_H_
#define _C0VM_ABORT_H_

/** @brief Report a user-level error (C0 @c error() call). */
void c0_user_error(char *err);

/** @brief Report a failed assertion (C0 @c assert() failure). */
void c0_assertion_failure(char *err);

/** @brief Report a memory error (null dereference, bad cast, etc.). */
void c0_memory_error(char *err);

/** @brief Report an arithmetic error (division/modulo by zero, overflow). */
void c0_arith_error(char *err);

#endif /* _C0VM_ABORT_H_ */
