#include <string.h>
#include <ctype.h>

#include "serv.h"

/*
https://datatracker.ietf.org/doc/html/rfc3986
*/

//
// До боли оптимизированная функция, но ОЧЕНЬ плохой стиль программирований
// Быстрее сделать почти не возможно, во всяком случае без оптимзации под кокретную архитектуру
// Возвращает указатель на поле путь HTTP заголовка
//

char * get_http_method(char * hdr, url_t * uri)
{
    const char G='G', E='E', T='T', P='P', U='U', O='O', S='S', SP=' ';
    char predicate;
    char * next = nullptr;
    uri->method = ERR;
    if (hdr[0] == G) {
        predicate = ((hdr[1] ^ E) | (hdr[2] ^ T) | (hdr[3] ^ SP));
        if (predicate == 0) {
            uri->method = GET;
            next = hdr + 4;
        }
    }
    else if (hdr[0] == P) {
        predicate = ((hdr[1] ^ U) | (hdr[2] ^ T) | (hdr[3] ^ SP));
        if (predicate == 0) {
            uri->method = PUT;
            next = hdr + 4;
        }
        else {
            predicate = ((hdr[1] ^ O) | (hdr[2] ^ S) | (hdr[3] ^ T) | (hdr[4] ^ SP));
            if (predicate == 0) {
                uri->method = POST;
                next = hdr + 5;
            }
            else if (strncmp(hdr, "PATCH ", 6) == 0) {
                uri->method = PATCH;
                next = hdr + 6;
            }
        }
    }
    else if (strncmp(hdr, "DELETE ", 7) == 0 ) {
        uri->method = DEL;
        next = hdr + 7;
    }
    return next;
}

inline char get_hex_digit(char ch)
{
    ch = toupper(ch);
    return ch >= 'A' ? 0xa + ch - 'A' : ch - '0';
}


//
// Копирует путь из HTTP заголовка в структуру utl_t
// Возвращает указатель на HTTP версию
//
char * get_http_path(char * ptr, url_t * uri)
{
    const char * allowed_in_path = "/-._~";
    uri->query_count = 0;
    for (int i = 0; i < 32; i++) {
        uri->query[i].key = nullptr;
        uri->query[i].val = nullptr;
    }
    for (int i = 0; i < max_path_len - 1; i++) {
        if (*ptr == ' ') {
            uri->path_len = i;
            uri->path[i] = '\0';
            return ++ptr;
        }
        if (*ptr == '?') {
            uri->path[i] = '\0';
            ++ptr;
            uri->query[uri->query_count].key = uri->path + i + 1;
            uri->query_count++;
            uri->path_len = i;
            bool key_found = true;
            for (i++; i < max_path_len; i++) {
                if (*ptr == ' ') {
                    uri->path[i] = '\0';
                    return ++ptr;
                }
                if (*ptr == '=' && key_found) {
                    uri->path[i] = '\0';
                    uri->query[uri->query_count-1].val = uri->path + i + 1;
                    ptr++;
                    key_found = false;
                    continue;
                }
                if (*ptr == '&') {
                    uri->path[i] = '\0';
                    uri->query[uri->query_count].key = uri->path + i + 1;
                    uri->query_count++;
                    ptr++;
                    key_found = true;
                    continue;
                }
                if (*ptr != '%')
                    uri->path[i] = *ptr++;
                else {
                    ptr++;
                    uri->path[i] = (get_hex_digit(*ptr++) << 4) | get_hex_digit(*ptr++);
                }
            }
        }
        if (*ptr != '%') {
            if (!isalnum(*ptr) && !strchr(allowed_in_path, *ptr)) {
                // fprintf(stderr, "Character '%c' not allowed in path\n", *ptr);
                break;
            }
            uri->path[i] = *ptr++;
        }
        else {
            ptr++;
            uri->path[i] = (get_hex_digit(*ptr++) << 4) | get_hex_digit(*ptr++);
        }
    }
    return nullptr;
}


char * get_http_version(char * p, url_t * uri)
{
    const char H = 'H', T = 'T', P = 'P', SL = '/', DOT = '.';
    char predicate = (p[0]^H) | (p[1]^T) | (p[2]^T) | (p[3]^P) | (p[4]^SL);
    predicate |= p[6] ^ DOT;
    
    if (predicate)
        return nullptr;

    uri->version.major = p[5] - '0';
    uri->version.minor = p[7] - '0';
    return p + 8;
}

char * parse_http_request(char * hdr, url_t * uri)
{
    char * next;
    next = get_http_method(hdr, uri);
    if (next) {
        next = get_http_path(next, uri);
        if (next)
            next = get_http_version(next, uri);
    }
    return next;
}
