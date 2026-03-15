/* parse.c - Parser for the clacc compiler.
 *
 * Reads a .clac source file, strips line comments, splits by semicolons
 * into function segments, tokenizes each segment, and resolves
 * user-defined function references. Produces a clac_file structure
 * consumed by the code generator (clacc.c).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/xalloc.h"
#include "lib/hdict.h"
#include "clacc.h"
#include "parse.h"

static void strip_line_comments(char *buf) {
    char *p = buf;
    while (*p) {
        if (*p == '/' && *(p + 1) == '/') {
            while (*p && *p != '\n') {
                *p = ' ';
                p++;
            }
        } else {
            p++;
        }
    }
}

/* hash dict functions */
bool key_equal(hdict_key x, hdict_key y) {
    return (strcmp((char*)x, (char*)y) == 0);
}

size_t key_hash(hdict_key x) {
    /* djb2 hash function */
    char *str = (char*)x;
    size_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash <<5) + hash) + c; /* hash * 33 + c*/
    return hash;

}
void uint16_t_free(hdict_value x) {
    free((uint16_t*)x);
}

bool operator2int(char *token, tok *parsedToken, hdict_t H) {
    parsedToken->raw = token;

    if (strcmp(token, "print") == 0) {
        parsedToken->operator = PRINT;
    } else if (strcmp(token, "quit") == 0) {
        parsedToken->operator = QUIT;
    } else if (strcmp(token, "+") == 0) {
        parsedToken->operator = PLUS;
    } else if (strcmp(token, "-") == 0) {
        parsedToken->operator = MINUS;
    } else if (strcmp(token, "*") == 0) {
        parsedToken->operator = MULT;
    } else if (strcmp(token, "/") == 0) {
        parsedToken->operator = DIV;
    } else if (strcmp(token, "%") == 0) {
        parsedToken->operator = MOD;
    } else if (strcmp(token, "<") == 0) {
        parsedToken->operator = LT;
    } else if (strcmp(token, "drop") == 0) {
        parsedToken->operator = DROP;
    } else if (strcmp(token, "swap") == 0) {
        parsedToken->operator = SWAP;
    } else if (strcmp(token, "rot") == 0) {
        parsedToken->operator = ROT;
    } else if (strcmp(token, "if") == 0) {
        parsedToken->operator = IF;
    } else if (strcmp(token, "pick") == 0) {
        parsedToken->operator = PICK;
    } else if (strcmp(token, "else") == 0) {
        parsedToken->operator = ELSE;
    } else if (strcmp(token, ":") == 0) {
        parsedToken->operator = USER_DEFINED; // start of function
    } else { // custom function
        void *func = hdict_lookup(H, (void*)token);

        if (func) {
            parsedToken->operator = UFUNC;
            parsedToken->i = (int32_t)(uint32_t)(*((uint16_t*)func));
        } else {
            /* fprintf(stderr, "Token not recognized: %s\n", token); */
            parsedToken->operator = UNK;
            /*return false; */
        }
    }

    return true;
}

bool tokenizeFunction(char *function, tokenList *tokens,
                      uint16_t functionIndex, hdict_t H,
                      char **name_out) {
    char *token = strtok(function, " \n");
    tokenList *currentToken = tokens;
    bool seen_func_header = false;

    if (name_out) *name_out = NULL;

    while (token) {
        char *next;
        long value = strtol(token, &next, 10);
        tok *parsedToken = xmalloc(sizeof(tok));

        if ((next == token) || (*next != '\0')) {
            if (!operator2int(token, parsedToken, H)) {
                fprintf(stderr, "Error parsing token %s", token);
                return false;
            }

            currentToken->next = xmalloc(sizeof(tokenList));
            currentToken->next->token = parsedToken;
            currentToken = currentToken->next;

            if (parsedToken->operator == USER_DEFINED) {
                if (seen_func_header) {
                    fprintf(stderr,
                            "Error: nested function definitions are "
                            "not allowed\n");
                    return false;
                }
                seen_func_header = true;

                token = strtok(NULL, " ");
                if (!token) {
                    fprintf(stderr,
                            "Error: missing function name after ':'\n");
                    return false;
                }
                strtol(token, &next, 10);

                if (!((next == token) || (*next != '\0'))) {
                    fprintf(stderr,
                            "Error: invalid function name '%s' "
                            "(must not be a number)\n", token);
                    return false;
                }

                if (name_out) *name_out = token;

                uint16_t *i = xmalloc(sizeof(uint16_t));
                *i = functionIndex;
                void *prev = hdict_insert(H, (void*)token, (void*)i);

                if (prev) {
                    free((uint16_t*)prev);
                }

                /* Comment functions: stop tokenizing -- body is free-form text */
                if (strcmp(token, "comment") == 0) {
                    currentToken->next = NULL;
                    return true;
                }
            }
        } else {
            parsedToken->operator = INT;
            parsedToken->i = (int32_t)value;
            currentToken->next = xmalloc(sizeof(tokenList));
            currentToken->next->token = parsedToken;
            currentToken = currentToken->next;
        }

        token = strtok(NULL, " \n");
    }

    currentToken->next = NULL;

    return true;
}

bool splitFile(char *buffer, clac_file *output, hdict_t H) {
    char *token = strtok(buffer, ";");
    list *currentFunction = output->functions;

    while (token) {
        currentFunction->next = xmalloc(sizeof(list));
        currentFunction = currentFunction->next;
        currentFunction->raw = token;
        currentFunction->name = NULL;
        output->functionCount++;
        token = strtok(NULL, ";");
    }
    currentFunction->next = NULL;
    fprintf(stderr, "Read %d functions.\n", output->functionCount);
    currentFunction = output->functions->next;
    uint16_t functionCount = 1;

    while (currentFunction != NULL) {
        tokenList *functionTokens = xmalloc(sizeof(tokenList));
        fprintf(stderr, "Tokenizing function %d\n", functionCount);

        char *func_name = NULL;
        if (tokenizeFunction(currentFunction->raw, functionTokens,
                             functionCount, H, &func_name)) {
            currentFunction->tokens = functionTokens;
            currentFunction->name = func_name;

            if (currentFunction->next == NULL)
                output->mainFunction = currentFunction->tokens;

            currentFunction = currentFunction->next;
            functionCount++;
        } else {
            fprintf(stderr, "Error tokenizing function %d\n",
                    functionCount);
            return false;
        }
    }

    fprintf(stderr, "Done tokenizing functions.\n");

    return true;
}

bool fixFunctionRefs(clac_file *output, hdict_t H) {
    list *currentFunction = output->functions->next;

    for (int i = 1; i <= output->functionCount; i++) {
        tokenList *currentToken = currentFunction->tokens->next;

        if (currentToken == NULL) {
            if (i == output->functionCount) {
                /* Empty main body -- not necessarily an error at parse time;
                 * the compiler will decide whether this is valid. */
                currentFunction = currentFunction->next;
                continue;
            }
            fprintf(stderr,
                    "Error: empty function body in segment %d\n", i);
            return false;
        }

        bool is_comment = (currentFunction->name &&
                           strcmp(currentFunction->name, "comment") == 0);

        while (currentToken != NULL) {
            if (currentToken->token->operator == UNK) {
                void *func = hdict_lookup(H,
                                          (void*)currentToken->token->raw);
                if (func) {
                    currentToken->token->operator = UFUNC;
                    currentToken->token->i =
                        (int32_t)(uint32_t)(*((uint16_t*)func));
                } else if (!is_comment) {
                    fprintf(stderr,
                            "Error: undefined identifier '%s'\n",
                            currentToken->token->raw);
                    return false;
                }
            }

            currentToken = currentToken->next;
        }

        currentFunction = currentFunction->next;
    }

    return true;
}

bool parse(char *path, clac_file *output) {
    FILE *F = fopen(path, "r");
    char *buffer = 0;
    long length;
    hdict_t H = hdict_new(100, &key_equal, &key_hash, &uint16_t_free);

    if (F) {
        fseek(F, 0, SEEK_END);
        length = ftell(F);
        fseek(F, 0, SEEK_SET);
        buffer = xmalloc(length + 1);

        if (buffer)  {
            fread(buffer, 1, length, F);
            buffer[length] = '\0';
        }

        fclose(F);
    } else {
        fprintf(stderr, "Error opening file %s\n", path);

        return false;
    }

    if (buffer) {
        strip_line_comments(buffer);
        if (splitFile(buffer, output, H) && fixFunctionRefs(output, H)) {
            /* does first pass, then second pass to fix function references,
             * and returns if success all in one line! */
            return true;
        } else {
            fprintf(stderr, "Error parsing file.\n");

            return false;
        }
    } else {
        fprintf(stderr, "Error opening file %s\n", path);

        return false;
    }

    hdict_free(H);
}
