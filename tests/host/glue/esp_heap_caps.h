#pragma once
#include <stddef.h>
#include <stdint.h>
#include "glue_alloc.h"
/* Host glue stubs: heap_caps_* go to the tracked allocator (glue_alloc.h). */
#define MALLOC_CAP_8BIT     (1u << 2)
#define MALLOC_CAP_SPIRAM   (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
