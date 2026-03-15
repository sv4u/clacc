/**
 * @file parse.h
 * @brief Parser interface for the clacc compiler.
 *
 * Provides the parse() entry point that reads a .clac source file,
 * tokenizes it, and resolves function references into a clac_file
 * structure suitable for code generation.
 */

#ifndef _PARSE_H_
#define _PARSE_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/xalloc.h"
#include "lib/hdict.h"
#include "clacc.h"

/**
 * @brief Parse a .clac source file into a clac_file structure.
 *
 * Reads the file at @p path, strips line comments, splits by semicolons
 * into function segments, tokenizes each segment, and resolves forward
 * references to user-defined functions via a two-pass approach.
 *
 * @param path    Path to the .clac source file.
 * @param output  Pre-allocated clac_file whose fields are populated on
 *                success. The caller must initialize @c output->functions
 *                to a sentinel node and @c output->functionCount to 0.
 * @return true on success, false on parse error (diagnostics printed to
 *         stderr).
 */
bool parse(char *path, clac_file *output);

#endif /* _PARSE_H_ */
