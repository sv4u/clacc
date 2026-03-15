#ifndef _PARSE_H_
#define _PARSE_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/xalloc.h"
#include "lib/hdict.h"
#include "clacc.h"

bool parse(char *path, clac_file *output);
/* Parses the .clac file at the given file path and populates the
 * fields of the clac_file at output.
 * Returns true on success, false on failure.
 */

#endif /* _PARSE_H_ */
