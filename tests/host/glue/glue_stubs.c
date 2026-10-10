// Host glue stubs (release-gate 1.2.36 pass1 С-2): ESP-IDF/FreeRTOS surface for main/*.c glue code on host.
// Tracked allocator (glue_alloc.h) counts every block of the code under test; the request recorder keeps what was sent.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "glue_alloc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

size_t glue_fail_above = SIZE_MAX;
int64_t glue_now_us = 1000000;
int glue_sem_depth = 0;
void (*glue_on_send)(httpd_req_t *r) = NULL;

static void *g_ptrs[512];
static size_t g_nptrs;

static void track_add(void *p) {
    if (g_nptrs == 512) {
        fprintf(stderr, "glue: too many blocks\n");
        exit(3);
    }
    g_ptrs[g_nptrs++] = p;
}

static void track_del(uintptr_t key) {   /* by address value: p itself must not be read after realloc() */
    for (size_t i = 0; i < g_nptrs; i++) {
        if ((uintptr_t)g_ptrs[i] == key) {
            g_ptrs[i] = g_ptrs[g_nptrs - 1];
            g_nptrs--;
            return;
        }
    }
    fprintf(stderr, "glue: free of untracked %#lx\n", (unsigned long)key);
    exit(3);
}

void *glue_realloc(void *p, size_t n) {
    if (n > glue_fail_above) return NULL;
    if (n == 0) n = 1;
    uintptr_t key = (uintptr_t)p;
    void *q = realloc(p, n);
    if (!q) return NULL;
    if (key) track_del(key);
    track_add(q);
    return q;
}

void *glue_malloc(size_t n) {
    return glue_realloc(NULL, n);
}

void glue_free(void *p) {
    if (!p) return;
    track_del((uintptr_t)p);
    free(p);
}

size_t glue_live_blocks(void) {
    return g_nptrs;
}

void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return glue_malloc(size);
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps) {
    (void)caps;
    return glue_realloc(ptr, size);
}

int64_t esp_timer_get_time(void) {
    return glue_now_us;
}

uint32_t esp_random(void) {
    return 0x12345678u;
}

void glue_req_init(httpd_req_t *r) {
    memset(r, 0, sizeof *r);
    r->status = 200;
}

void glue_req_free(httpd_req_t *r) {
    free(r->body);
    memset(r, 0, sizeof *r);
}

static void body_append(httpd_req_t *r, const char *buf, size_t len) {
    size_t needed = r->body_len + len + 1;
    if (r->body_cap < needed) {
        size_t new_cap = r->body_cap ? r->body_cap : 1024;
        while (new_cap < needed) new_cap *= 2;
        char *new_body = realloc(r->body, new_cap);
        if (!new_body) {
            fprintf(stderr, "glue: body realloc failed\n");
            exit(3);
        }
        r->body = new_body;
        r->body_cap = new_cap;
    }
    memcpy(r->body + r->body_len, buf, len);
    r->body_len += len;
    r->body[r->body_len] = 0;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len) {
    r->sends++;
    if (buf && len > 0) body_append(r, buf, (size_t)len);
    if (glue_on_send) glue_on_send(r);
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len) {
    if (buf == NULL || len == 0) {
        r->terminated++;
        return ESP_OK;
    }
    r->chunks++;
    body_append(r, buf, (size_t)len);
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t code, const char *msg) {
    r->status = (int)code;
    (void)msg;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value) {
    if (strcmp(field, "X-Spectrum-Render-Count") == 0) {
        snprintf(r->render_hdr, sizeof r->render_hdr, "%s", value);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    (void)r;
    (void)type;
    return ESP_OK;
}

static int g_sem_objs[16];
static int g_nsem;

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (g_nsem == 16) return NULL;
    return &g_sem_objs[g_nsem++];
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (g_nsem == 16) return NULL;
    return &g_sem_objs[g_nsem++];
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
    (void)t;
    if (!s) return pdTRUE;
    glue_sem_depth++;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    if (!s) return pdTRUE;
    glue_sem_depth--;
    return pdTRUE;
}

void vTaskDelay(TickType_t t) {
    (void)t;
}

BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, void *handle, int core) {
    (void)fn;
    (void)name;
    (void)stack;
    (void)arg;
    (void)prio;
    (void)handle;
    (void)core;
    return pdPASS;
}
