#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"
/* Host glue stubs: the request records what the handler sent (status, body, chunks, render header). */
typedef enum {
    HTTPD_400_BAD_REQUEST = 400, HTTPD_404_NOT_FOUND = 404, HTTPD_414_URI_TOO_LONG = 414,
    HTTPD_500_INTERNAL_SERVER_ERROR = 500
} httpd_err_code_t;
typedef struct httpd_req {
    int status;            /* 200 unless httpd_resp_send_err() was called */
    char *body; size_t body_len; size_t body_cap;
    int sends, chunks, terminated;
    char render_hdr[24];
    /* 1.2.37: every httpd_resp_set_hdr() pair in call order (like ra->resp_hdrs of esp_http_server: field/value are kept as given, count is capped by
       max_resp_headers = 8 of HTTPD_DEFAULT_CONFIG); hdr_n counts all calls, pairs beyond 16 are counted but not stored */
    int hdr_n;
    char hdr_field[16][40], hdr_value[16][32];
    const char *query;     /* pass2 З-3: query string without '?' (NULL - no query) */
    int sockfd;            /* 1.2.37: fake socket of the request (httpd_req_to_sockfd), glue_req_init sets 7 */
} httpd_req_t;
#define ESP_ERR_HTTPD_RESULT_TRUNC 0xb004   /* IDF v5.4 esp_http_server.h:86 */
size_t httpd_req_get_url_query_len(httpd_req_t *r);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t buf_len);
/* pass2 С-1: when not NULL, called by httpd_resp_send() after the body is recorded (a second request during a send) */
extern void (*glue_on_send)(httpd_req_t *r);
void glue_req_init(httpd_req_t *r);
void glue_req_free(httpd_req_t *r);
int httpd_req_to_sockfd(httpd_req_t *r);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t code, const char *msg);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
