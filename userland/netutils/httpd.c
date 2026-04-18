/*
 *      httpd - minimal HTTP server for Frosted.
 *
 *      Modeled after wolfIP's src/http/httpd.c (same URL-registration
 *      pattern), but uses BSD sockets so it runs as a regular Frosted
 *      userland app. TLS is not enabled — add a wolfSSL wrap around
 *      accept() when we want HTTPS.
 *
 *      Copyright (c) 2026 The Frosted authors.
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define HTTPD_PORT          80
#define HTTPD_MAX_URLS      16
#define HTTPD_BACKLOG       4
#define HTTP_METHOD_LEN     8
#define HTTP_PATH_LEN       128
#define HTTP_RECV_BUF_LEN   1460
#define HTTP_TX_BUF_LEN     1460

struct http_request {
    char method[HTTP_METHOD_LEN];
    char path[HTTP_PATH_LEN];
};

struct http_url {
    char path[HTTP_PATH_LEN];
    const char *static_content;
};

struct httpd {
    struct http_url urls[HTTPD_MAX_URLS];
    int listen_sd;
    uint16_t port;
};

static int httpd_init(struct httpd *h, uint16_t port)
{
    struct sockaddr_in sa;
    int sd, on = 1;

    memset(h, 0, sizeof(*h));
    h->port = port;
    h->listen_sd = -1;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("socket");
        return -1;
    }
    (void)setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(sd);
        return -1;
    }
    if (listen(sd, HTTPD_BACKLOG) < 0) {
        perror("listen");
        close(sd);
        return -1;
    }
    h->listen_sd = sd;
    return 0;
}

static int httpd_register_static_page(struct httpd *h, const char *path,
                                      const char *content)
{
    int i;
    for (i = 0; i < HTTPD_MAX_URLS; i++) {
        if (h->urls[i].path[0] == 0) {
            strncpy(h->urls[i].path, path, HTTP_PATH_LEN - 1);
            h->urls[i].path[HTTP_PATH_LEN - 1] = 0;
            h->urls[i].static_content = content;
            return 0;
        }
    }
    return -1;
}

static int parse_request(const char *buf, int len, struct http_request *req)
{
    int i = 0, j;
    memset(req, 0, sizeof(*req));
    for (j = 0; j < HTTP_METHOD_LEN - 1 && i < len && buf[i] != ' '; j++, i++)
        req->method[j] = buf[i];
    if (i >= len || buf[i] != ' ')
        return -1;
    i++;
    for (j = 0; j < HTTP_PATH_LEN - 1 && i < len && buf[i] != ' ' && buf[i] != '\r'; j++, i++)
        req->path[j] = buf[i];
    if (req->method[0] == 0 || req->path[0] == 0)
        return -1;
    return 0;
}

static const struct http_url *find_url(const struct httpd *h, const char *path)
{
    int i;
    for (i = 0; i < HTTPD_MAX_URLS; i++) {
        if (h->urls[i].path[0] == 0)
            break;
        if (strcmp(h->urls[i].path, path) == 0)
            return &h->urls[i];
    }
    return NULL;
}

static void send_response(int sd, int status, const char *status_text,
                          const char *content_type, const char *body,
                          size_t body_len)
{
    char hdr[256];
    int n;
    n = snprintf(hdr, sizeof(hdr),
                 "HTTP/1.1 %d %s\r\n"
                 "Server: frosted-httpd\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 status, status_text, content_type, (unsigned)body_len);
    if (n > 0)
        (void)send(sd, hdr, n, 0);
    if (body && body_len)
        (void)send(sd, body, body_len, 0);
}

static void handle_client(const struct httpd *h, int cs)
{
    char buf[HTTP_RECV_BUF_LEN];
    int n;
    struct http_request req;
    const struct http_url *u;

    n = recv(cs, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        goto done;
    buf[n] = 0;
    if (parse_request(buf, n, &req) < 0) {
        send_response(cs, 400, "Bad Request", "text/plain",
                      "bad request\n", 12);
        goto done;
    }
    u = find_url(h, req.path);
    if (!u) {
        send_response(cs, 404, "Not Found", "text/plain",
                      "not found\n", 10);
        goto done;
    }
    send_response(cs, 200, "OK", "text/html",
                  u->static_content, strlen(u->static_content));
done:
    close(cs);
}

static const char home_html[] =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\">\n"
    "  <title>Frosted</title>\n"
    "  <style>\n"
    "    body{font-family:system-ui,sans-serif;background:#0b1220;color:#e0f2ff;\n"
    "         margin:0;display:flex;align-items:center;justify-content:center;\n"
    "         min-height:100vh}\n"
    "    main{max-width:38em;padding:2em;text-align:center}\n"
    "    h1{font-size:2.6em;margin:0 0 .3em;letter-spacing:.02em}\n"
    "    p{margin:.4em 0;color:#b8d4e8}\n"
    "    code{background:#0f1a2e;padding:.1em .4em;border-radius:.3em;color:#8fd1ff}\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <main>\n"
    "    <h1>frosted</h1>\n"
    "    <p>tiny POSIX RTOS for ARMv8-M</p>\n"
    "    <p>served by <code>httpd</code> running on wolfIP</p>\n"
    "  </main>\n"
    "</body>\n"
    "</html>\n";

int main(int argc, char *argv[])
{
    struct httpd h;
    uint16_t port = HTTPD_PORT;

    if (argc >= 2) {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536)
            port = (uint16_t)p;
    }

    if (httpd_init(&h, port) < 0)
        return 1;
    httpd_register_static_page(&h, "/", home_html);

    printf("httpd: listening on port %u\n", (unsigned)port);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cs = accept(h.listen_sd, (struct sockaddr *)&peer, &plen);
        if (cs < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        handle_client(&h, cs);
    }
    close(h.listen_sd);
    return 0;
}
