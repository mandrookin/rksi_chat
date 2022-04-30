#ifndef _SERV_H_
#define _SERV_H_

typedef enum {
    ERR = -1,
    POST = 0,
    GET,
    PUT,
    PATCH,
    DEL
} http_method_t;

typedef enum {
    v0_9,
    v1_0,
    v1_1,
    v2_0
} http_version_t;

typedef union {
    http_version_t      human;
    struct {
        short           major;
        short           minor;
    };
} http_hr_version_t;

const int max_path_len = 1024;

typedef struct {
    char    *   key;
    char    *   val;
} key_val_t;

typedef struct {
    http_method_t       method;
    http_hr_version_t   version;
    int                 path_len;
    int                 query_count;
    key_val_t           query[32];
    char                path[max_path_len];
} url_t;


char * parse_http_request(char * hdr, url_t * uri);

#endif
