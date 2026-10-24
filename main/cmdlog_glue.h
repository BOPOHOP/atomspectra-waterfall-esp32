#ifndef CMDLOG_GLUE_H
#define CMDLOG_GLUE_H

#include <stdint.h>
#include <stddef.h>

#ifdef AWF_CMDLOG

uint32_t cmdlog_glue_record(uint8_t src, const char *text, size_t len, int term_nul);
void     cmdlog_glue_result(uint32_t seq, int delivered, int rc);
size_t   cmdlog_glue_json(uint32_t since, char *out, size_t cap);
void     cmdlog_glue_start(void);
uint8_t  cmdlog_glue_src_from_task(void);

#else

static inline uint32_t cmdlog_glue_record(uint8_t src, const char *text, size_t len, int term_nul) { (void)src; (void)text; (void)len; (void)term_nul; return 0; }
static inline void     cmdlog_glue_result(uint32_t seq, int delivered, int rc) { (void)seq; (void)delivered; (void)rc; }
static inline size_t   cmdlog_glue_json(uint32_t since, char *out, size_t cap) { (void)since; (void)out; (void)cap; return 0; }
static inline void     cmdlog_glue_start(void) {}
static inline uint8_t  cmdlog_glue_src_from_task(void) { return 5; }

#endif

#endif /* CMDLOG_GLUE_H */

