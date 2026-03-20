#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lwip/serotonin/lwip_client.h"

#define WEBROOT "/srv"

static const char *ip_to_str(uint32_t addr, char *buf) {
    unsigned a = addr & 0xff, b = (addr >> 8) & 0xff;
    unsigned c = (addr >> 16) & 0xff, d = (addr >> 24) & 0xff;
    sprintf(buf, "%u.%u.%u.%u", a, b, c, d);
    return buf;
}

static const char *guess_content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html";
    if (strcmp(dot, ".css") == 0)  return "text/css";
    if (strcmp(dot, ".js") == 0)   return "application/javascript";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)  return "image/gif";
    if (strcmp(dot, ".txt") == 0)  return "text/plain";
    if (strcmp(dot, ".json") == 0) return "application/json";
    return "application/octet-stream";
}


static int parse_request_path(const char *req, char *path, int pathsize) {
    const char *p = strchr(req, ' ');
    if (!p) return -1;
    p++;

    const char *end = strchr(p, ' ');
    if (!end) end = strchr(p, '\r');
    if (!end) end = strchr(p, '\n');
    if (!end) return -1;

    int len = (int)(end - p);
    if (len <= 0 || len >= pathsize) return -1;
    memcpy(path, p, len);
    path[len] = '\0';

    char *q = strchr(path, '?');
    if (q) *q = '\0';

    return 0;
}

static void send_error(lwip_session_t *s, uint8_t handle, int code, const char *status, const char *msg) {
    char body[256];
    int blen = snprintf(body, sizeof(body),
        "<html><body><h1>%d %s</h1><p>%s</p></body></html>\n",
        code, status, msg);

    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", code, status, blen);

    lwip_tcp_send(s, handle, hdr, (uint16_t)hlen);
    lwip_tcp_send(s, handle, body, (uint16_t)blen);
}

static void handle_client(lwip_session_t *s, uint8_t handle, uint32_t client_addr, uint16_t client_port) {
    char ipbuf[16];
    printf("httpd: %s:%u ",
           ip_to_str(client_addr, ipbuf), (unsigned)client_port);

    char reqbuf[1024];
    int n = lwip_tcp_recv(s, handle, reqbuf, sizeof(reqbuf) - 1);
    if (n <= 0) {
        printf("(empty request)\n");
        lwip_tcp_close(s, handle);
        return;
    }
    reqbuf[n] = '\0';

    char uri[256];
    if (parse_request_path(reqbuf, uri, sizeof(uri)) < 0) {
        printf("(bad request)\n");
        send_error(s, handle, 400, "Bad Request", "Could not parse request.");
        lwip_tcp_close(s, handle);
        return;
    }

    printf("%s\n", uri);

    if (strstr(uri, "..")) {
        send_error(s, handle, 403, "Forbidden", "Path not allowed.");
        lwip_tcp_close(s, handle);
        return;
    }

    char filepath[512];
    if (strcmp(uri, "/") == 0)
        snprintf(filepath, sizeof(filepath), "%s/index.html", WEBROOT);
    else
        snprintf(filepath, sizeof(filepath), "%s%s", WEBROOT, uri);

    FILE *fp = fopen(filepath, "r");

    if (!fp && uri[strlen(uri) - 1] == '/') {
        snprintf(filepath, sizeof(filepath), "%s%sindex.html", WEBROOT, uri);
        fp = fopen(filepath, "r");
    }

    if (!fp) {
        send_error(s, handle, 404, "Not Found", "File not found.");
        lwip_tcp_close(s, handle);
        return;
    }

    static char filebuf[8192];
    int flen = fread(filebuf, 1, sizeof(filebuf), fp);
    fclose(fp);

    const char *ctype = guess_content_type(filepath);
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", ctype, flen);

    lwip_tcp_send(s, handle, hdr, (uint16_t)hlen);

    int sent = 0;
    while (sent < flen) {
        int chunk = flen - sent;
        if (chunk > 1400) chunk = 1400;
        lwip_tcp_send(s, handle, filebuf + sent, (uint16_t)chunk);
        sent += chunk;
    }

    lwip_tcp_close(s, handle);
}

int main(int argc, char **argv) {
    uint16_t port = 80;
    if (argc >= 2)
        port = (uint16_t)atoi(argv[1]);

    lwip_session_t *s = lwip_session_open();
    if (!s) {
        printf("httpd: cannot connect to lwipd\n");
        return 1;
    }

    int lh = lwip_tcp_listen(s, port);
    if (lh < 0) {
        printf("httpd: cannot listen on port %u\n", (unsigned)port);
        lwip_session_close(s);
        return 1;
    }

    printf("httpd: serving %s on port %u\n", WEBROOT, (unsigned)port);

    for (;;) {
        uint32_t client_addr;
        uint16_t client_port;
        int ah = lwip_tcp_accept(s, (uint8_t)lh, &client_addr, &client_port);
        if (ah < 0) {
            printf("httpd: accept failed\n");
            continue;
        }

        handle_client(s, (uint8_t)ah, client_addr, client_port);
    }

    lwip_tcp_close(s, (uint8_t)lh);
    lwip_session_close(s);
    return 0;
}
