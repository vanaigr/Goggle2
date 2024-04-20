#include<assert.h>
#include<stdio.h>
#include<string.h>

#include<stdint.h>
#include<stdbool.h>

#include<windows.h>
#include<winhttp.h>
//#include<winsock2.h>

#include<sha1/sha1.h>

#include"common.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

static const char page_header_msg[]
    = "HTTP/1.1 200 OK\r\n"
    "Content-type: text/html\r\n"
    "\r\n<!DOCTYPE html><html><head></head>";
int const page_header_c = STR_SIZE(page_header_msg);
static char const not_found_msg[] = "HTTP/1.1 404 Not Found\r\n\r\n";
int const not_found_c = STR_SIZE(not_found_msg);
static char const not_impl_msg[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
int const not_impl_c = STR_SIZE(not_impl_msg);

static char const get[] = "GET";
static char const search[] = "/search?q=";

static char *page_msg;
static int page_c;

static SOCKET server_socket;
static SOCKET websockets_sockets[64];
static int websockets_c = 0;

static HINTERNET winhttp_state = NULL;
static HINTERNET ggl_conn = NULL;

static char const head_cmp[] = "<head>";
static int const head_cmp_c = STR_SIZE(head_cmp);
static char const end_head_cmp[] = "</head>";
static int const end_head_cmp_c = STR_SIZE(end_head_cmp);

enum {
    tag_stack_size = 256,
    result_size = 65536,
    tmp_size = 1024 * 1024,
};
static uint8_t client_request[2048] = "";
static uint32_t result_c;
static char result[result_size];
static char tmp[tmp_size];

static char const req_obj_search[] = "/search";
static char const req_obj_ws[] = "/ws";
static int const req_obj_search_size = STR_SIZE(req_obj_search);
static int const req_obj_ws_size = STR_SIZE(req_obj_ws);

Result send_complete_(char const *msg, int size, SOCKET sock, char *file, int line) {
    int off = 0;
    while(off < size) {
        int res = send(sock, msg + off, size - off, 0);
        if(res == SOCKET_ERROR) {
            printf("Error while sending at `%s`:%d  %d", file, line, WSAGetLastError());
            return ERR;
        }
        off += res;
    }
    return OK;
}


Result send_main_page(SOCKET sock) {
    return send_complete(page_msg, page_c, sock);
}

static void handle_server_socket(void) {
    SOCKET conn_socket = accept(server_socket, NULL, NULL);
    if (conn_socket == INVALID_SOCKET) {
        printf("Accept failed with error code : %d", WSAGetLastError());
        goto error;
    }

    int received = recv(conn_socket, (char*)client_request, 2047, 0);
    if(received < 0) {
        printf("Error receiving\n");
        goto error;
    }
    printf("Received %d bytes: \n`%.*s`\n", received, received, client_request);
    client_request[received] = '\0';

    uint8_t *req_obj_b = client_request;
    while(true) {
        if(*req_obj_b == ' ') { // only one space
            req_obj_b++;
            break;
        }
        if(*req_obj_b == '\0') {
            printf("Truncated request w/o object\n");
            goto error;
        }
        req_obj_b++;
    }
    uint8_t *req_obj_e = req_obj_b;
    while(true) {
        if(*req_obj_e == ' ' || *req_obj_e == '\0') break;
        req_obj_e++;
    }
    int req_obj_size = req_obj_e - req_obj_b;

    if(req_obj_size >= req_obj_search_size
        && memcmp(req_obj_b, req_obj_search, req_obj_search_size) == 0
    ) {
        result_c = extract(
            conn_socket, ggl_conn,
            req_obj_b + req_obj_search_size + 1, req_obj_e,
            tmp, tmp + tmp_size,
            result, result + result_size
        );
    }
    if(req_obj_size == req_obj_ws_size
        && memcmp(req_obj_b, req_obj_ws, req_obj_ws_size) == 0
    ) {
        static char const header_msg[]
            = "HTTP/1.1 101 Switching Protocols\r\n"
            "Connection: Upgrade\r\nUpgrade: websocket\r\n"
            "Sec-WebSocket-Accept: ";
        int const header_msg_c = STR_SIZE(header_msg);

        static char const b64lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        static char const key_header_name[] = "Sec-WebSocket-Key";
        int const key_header_c = STR_SIZE(key_header_name);

        char const *pos = strstr((char*)client_request, key_header_name);
        if(pos == NULL) {
            printf("No websocket key\n");
            goto error;
        }

        // same for everyone
        static char const key2[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        int const key2_len = STR_SIZE(key2);
        int key1_len = 60 - key2_len;

        if(pos - (char*)client_request + key_header_c + 2 + key1_len > received) {
            printf("Incorrect size\n");
            goto error;
        }
        if(memcmp(pos + key_header_c, ": ", 2)) {
            printf("incorrect ': '");
            goto error;
        }
        // skip '<name>: '
        pos += key_header_c + 2;

        uint8_t *digest = (uint8_t*)tmp;
        uint8_t *key = digest;
        char *response = (char*)digest;

        memcpy(key, pos, key1_len);
        memcpy(key + key1_len, key2, key2_len);
        *(key + key1_len + key2_len) = '\0';

        SHA1_CTX ctx;
        SHA1Init(&ctx);
        SHA1Update(&ctx, key, 60);
        SHA1Final(digest, &ctx);

        char *accept = response + header_msg_c;

        // y big endian...
#define D(i, o) ((uint32_t)digest[(i) * 3 + (o)] << (24 - (o) * 8))
        for(int i = 0; i < 6; i++) {
            uint32_t v = D(i, 0) | D(i, 1) | D(i, 2);
            for(int j = 0; j < 4; j++) {
                *accept++ = b64lookup[(v >> (32-6)) & 63u];
                v <<= 6;
            }
        }
#undef D
        {
            uint32_t v = ((uint32_t)digest[18] << 16) | ((uint32_t)digest[19] << 8);
            for(int j = 0; j < 3; j++) {
                *accept++ = b64lookup[(v >> (24-6)) & 63u];
                v <<= 6;
            }
            *accept++ = '=';
        }

        memcpy(response, header_msg, header_msg_c);
        memcpy(accept, "\r\n\r\n", 4);

        uint8_t *websock_response = (uint8_t*)accept + 4;
        websock_response[0] = (uint8_t)((1u << 7) | 2);
        int websock_response_size;
        if(result_c == -1) {
            websock_response[1] = 1;
            websock_response[2] = 5;
            websock_response_size = 3;
        }
        else {
            assert(result_c < 65536);
            websock_response[1] = 126;
            websock_response[2] = (uint8_t)(result_c >> 8);
            websock_response[3] = (uint8_t)result_c;
            memcpy(websock_response + 4, result, result_c);
            websock_response_size = 4 + result_c;
        }

        char *msg = response;
        int msg_c = websock_response + websock_response_size - (uint8_t*)response;
        if(send_complete(msg, msg_c, conn_socket).err) goto error;
        websockets_sockets[websockets_c++] = conn_socket;
        printf("Sent result");

        return;
    }
    else {
        printf("Not sending anything\n");
    }

error:
    printf("Connection closed\n");
    closesocket(conn_socket);
}

static int setup_page(void) {
    FILE *file = fopen("src/answers.html", "rb");
    if(file == NULL) {
        printf("Error opening page file\n");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    int const file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    page_c = page_header_c + file_size;
    page_msg = malloc(page_c);
    if(!page_msg) {
        printf("RAM?");
        return 1;
    }

    memcpy(page_msg, page_header_msg, page_header_c);
    int read = fread(page_msg + page_header_c, sizeof(char), file_size, file);
    if(read != file_size || read == 0) {
        printf("PAGE? %d %d", read, file_size);
        return 1;
    }
    printf("Read %d bytes\n", read);
    fclose(file);

    return 0;
}

int main(int argc, char **argv) {
    WSADATA wsa;

    printf("\nInitialising Winsock...");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code: %d", WSAGetLastError());
        return 1;
    }

    winhttp_state = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
    );
    if(!winhttp_state) {
        printf("Failed to init winhttp %ld\n", GetLastError());
        return 1;
    }

    ggl_conn = WinHttpConnect(
        winhttp_state, L"www.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0
    );
    if (!ggl_conn) {
        printf("Failed to connect to google: %lu\n", GetLastError());
        return 1;
    }

    printf("Initialised.\n");

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("Could not create socket: %d", WSAGetLastError());
        return 1;
    }

    struct sockaddr_in server = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = INADDR_ANY },
        .sin_port = htons(1619),
    };

    if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed with error code: %d", WSAGetLastError());
        return 1;
    }

    if(setup_page()) {
        printf("No page!\n");
        return 1;
    }

    listen(server_socket, 60);

    fd_set read_fs;
    fd_set error_fs;

    while(true) {
        printf("\nWaiting for incoming connections...\n");

        FD_ZERO(&read_fs);
        FD_ZERO(&error_fs);
        FD_SET(server_socket, &read_fs);
        FD_SET(server_socket, &error_fs);
        for(int i = 0; i < websockets_c; i++) {
            SOCKET *s = &websockets_sockets[i];
            FD_SET(*s, &read_fs);
            FD_SET(*s, &error_fs);
        }

        int ready_c = select(0, &read_fs, NULL, &error_fs, NULL);
        if(ready_c == SOCKET_ERROR) {
            printf("Select failed: %d", WSAGetLastError());
            return 1;
        }
        else if(ready_c <= 0) {
            printf("Some error with select(): %d\n", ready_c);
            return 1;
        }
        if(error_fs.fd_count != 0) {
            printf("Some error with sockets: %d\n", ready_c);
            return 1;
        }

        if(FD_ISSET(server_socket, &read_fs)) {
            handle_server_socket();
        }

        // is this quadratic bc of FD_ISSET?
        for(int i = websockets_c-1; i != -1; i--) {
            SOCKET s = websockets_sockets[i];
            if(!FD_ISSET(s, &read_fs)) continue;

            printf("Websocket received something!\n");

            int received = recv(s, client_request, 2047, 0);
            if(received < 0) {
                printf("Error receiving\n");
                return 32;
            }

            if(received < 1 || client_request[0] != 0x88) {
                // 8 for no continuation, 8 for close
                printf("first byte %x is not expected\n", client_request[0]);
                return 52;
            }
            if(received < 2 || (client_request[1] & 0x80) == 0) {
                printf("Message not masked\n");
                return 52;
            }
            int len = client_request[1] & 0x7fu;
            if(len > 125) {
                printf("Unsupported length %d\n", len);
                return 52;
            }
            if(received < 6) {
                printf("Mask where\n");
                return 52;
            }
#define M(index) client_request[index]
            uint8_t mask[4] = { M(2), M(3), M(4), M(5) };
#undef M
            if(received < 6 + len) {
                printf("Message len is smaller than currently available\n");
                return 52;
            }
            if(received > 2048) {
                printf("Message too big\n");
                return 52;
            }
            uint8_t *client_msg = client_request + 6;
            uint8_t *response = (uint8_t*)tmp;
            uint8_t *decoded_msg = response + 2;
            for(int i = 0; i < len; i++) {
                decoded_msg[i] = client_msg[i] ^ mask[i % 4];
            }

            printf("Message: %.*s\n", len, decoded_msg);

            printf("Request: `");
            for(int i = 0; i != received; i++) {
                printf("%c", client_request[i]);
            }
            printf("`\n");

            response[0] = (uint8_t)((1 << 7) | 8);
            response[1] = len;
            if(send_complete((char*)response, 2 + len, s).err) {
                printf("Error while sending websocket close\n");
            }
            else {
                printf("Sent closing websocket response\n");
            }

            closesocket(s);
            websockets_c--;
            if(websockets_c > i) {
                websockets_sockets[i] = websockets_sockets[websockets_c];
            }
        }
    }

    //closesocket(server_socket);
    //WSACleanup();
    //WinHttpCloseHandle(ggl_conn);
    //WinHttpCloseHandle(winhttp_state);

    return 0;
}
