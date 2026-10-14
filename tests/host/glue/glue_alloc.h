#pragma once
#include <stddef.h>
/* Host glue: tracked allocator. A test includes main/<file>.c after
 *   #define malloc(n) glue_malloc(n) / realloc(p, n) glue_realloc(p, n) / free(p) glue_free(p)
 * so that every block of the glue code is counted (leak = glue_live_blocks() grows).
 * glue_fail_above: a request larger than this many bytes fails (NULL, old block untouched). */
extern size_t glue_fail_above;
void *glue_malloc(size_t n);
void *glue_realloc(void *p, size_t n);
void glue_free(void *p);
size_t glue_live_blocks(void);
