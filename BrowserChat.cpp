#define _CRT_SECURE_NO_WARNINGS 1

#include <stdio.h>
#include <winsock2.h>
#include "serv.h"

#include "ChatClient.h"

const char * http_response_header =
    "HTTP/1.1 200 OK\r\n"
    "Server: RKSI/0.0\r\n"
    "Connection: close\r\n"
    "Date: Sun, 18 Apr 2022 10:36:20 GMT\r\n"
    "Content-Length: %d\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "\r\n";

const char * http_not_found =
    "HTTP/1.1 404 Not found\r\n"
    "Connection: close\r\n"
    "Server: RKSI/0.0\r\n"
    "Date: Sun, 18 Apr 2022 10:36:20 GMT\r\n"
    "Content-Length: 21\r\n"
    "\r\n"
    "Content not present\r\n";

const char * http_response_body =
    "<html>"
    "<head>"
    "<title>Use telnet!</title>"
    "</head>"
    "<body>"
    "<h>This server does not support HTTP.</h> <br/><p>Use telnet</p>"
    "</body>"
    "</html>";

int PositiveReposne(char * buff, int buff_size)
{
    int header_len, body_len, total_len;
    body_len = strlen(http_response_body);
    header_len = snprintf(buff, buff_size, http_response_header, body_len);
    total_len = header_len + body_len;
    snprintf(buff + header_len, buff_size - body_len,  "%s", http_response_body);
    return total_len;
}

int NegativeResponse(char * buff, int buff_size)
{
    strncpy(buff, http_not_found, buff_size);
    return strlen(buff);
}

int BrowserClient::HTTP_Response(url_t *url)
{
    int len;
    char http_buffer[4096];

    if (url->method == GET && strcmp(url->path, "/") == 0)
        len = PositiveReposne(http_buffer, sizeof(http_buffer));
    else
        len = NegativeResponse(http_buffer, sizeof(http_buffer));

    SendData(http_buffer, len);
    return len;
}

void BrowserClient::DebugURL()
{
    printf("REQ=%d, L=%d, PATH=\"%s\", V=%04x\n", url.method, url.path_len, url.path, url.version.human);
    for (int i = 0; i < url.query_count; i++)
        printf("\tQK=%s  QV=%s\n", url.query[i].key, url.query[i].val ? url.query[i].val : "");
}


int BrowserClient::RunChat()
{
    int len;
    char http_buffer[4096];

    HTTP_Response(&url);

    while (true) 
    {
        len = tcp->ReceiveData(http_buffer, sizeof(http_buffer), 500);
        if (len < 0) {
            printf("HTTP client socket error on receive\n");
            break;
        }
        if (len == 0) {
            printf("HTTP client closed connection\n");
            break;
        }
        http_buffer[len] = 0;
        printf("HTTP client receive %d bytes: %s\n", len, http_buffer);
        if (len > 0) {
            char * next = parse_http_request(http_buffer, &url);
            if (next) {
                DebugURL();
            }
            HTTP_Response(&url);
        }
    }
    CloseSocket();
    return len;
}

// Это конструктор
BrowserClient::BrowserClient(TcpConnection * tcp, url_t u, char * header) : ChatClient(tcp)
{
    url = u;

#if ! MY_DEBUG
    DebugURL();
#endif

    // TODO: Распарсить заголов HTTP пакета
}

