#include<assert.h>
#include<stdio.h>
#include<string.h>

#include<stdint.h>
#include<stdbool.h>

#include<windows.h>
#include<winhttp.h>
//#include<winsock2.h>

#include<sha1/sha1.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

#define ARR_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))
#define STR_SIZE(str) (sizeof((str)) - 1)

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

static char const head_cmp[] = "<head>";
static int const head_cmp_c = STR_SIZE(head_cmp);
static char const end_head_cmp[] = "</head>";
static int const end_head_cmp_c = STR_SIZE(end_head_cmp);

enum {
    tag_stack_size = 2048,
    result_tmp_size = 65536,
    page_tmp_size = 1024 * 1024,
};
static uint8_t client_request[2048] = "";
static char result_tmp[result_tmp_size];
static char page_tmp[page_tmp_size];
static uint32_t tag_stack[tag_stack_size];

typedef unsigned short wchar;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static int get_diff(char const *b1, char const *b2, int len) {
    for(int i = 0; i < len; i++) {
        if(b1[i] != b2[i]) return i;
    }
    return len;
}

static void handle_server_socket(void) {
#if 0
    SOCKET conn_socket = accept(server_socket, NULL, NULL);
    if (conn_socket == INVALID_SOCKET) {
        printf("Accept failed with error code : %d", WSAGetLastError());
        goto error;
    }

    int received = recv(conn_socket, client_request, 2047, 0);
    if(received < 0) {
        printf("Error receiving\n");
        goto error;
    }
    printf("Received %d bytes: `%.*s`\n", received, received, client_request);
    client_request[received] = '\0';


    if(received < 3 || memcmp(client_request, get, 3) != 0) {
        printf("Response not Implemented\n");
        char *msg = not_impl_msg;
        int msg_c = not_impl_c;

        int sent = send(conn_socket, msg, msg_c, 0);
        printf("Sent %d of %d\n", sent, msg_c);
    }
    else if(received >= 14 && memcmp(client_request + 4, search, 10) == 0) {
        printf("Returning search page\n");
        static char const object[] = "/search?q=";
        int object_c = STR_SIZE(object);

        //int sent = send(conn_socket, page_header_msg, page_header_c, 0);
        //assert(sent == page_header_c);
        strcpy(client_request, "boba ");

        memcpy(tmp, object, object_c);
        int search_c = 0;
        while(true) {
            uint8_t const v = client_request[search_c];
            if(v == ' ' || v == '\0' || v == '\r') {
                break;
            }
            assert(object_c + search_c < 2048);
            tmp[object_c + search_c] = v;
            search_c++;
        }
        // https://github.com/searxng/searxng/issues/159
        // Note: to filter out useful results, use <div jscontroller="SC7lYd"
        // (this if from SearXNG engines/google.py results_xpath)
        // Example: https://google.com/search?q=abc&asearch=arc&async=use_ac:true,_fmt:prog
        static char const extra[] = "&asearch=arc&async=use_ac:true,_fmt:prog";
        int const extra_c = STR_SIZE(extra);
        memcpy(tmp + object_c + search_c, extra, extra_c + 1); // with \0
        printf("URL: %s\n", tmp);

        static char const hdrs[]
            =
            "accept: */*\r\n"
         ;

        HINTERNET ggl_conn = InternetConnectA(
            winhttp_state, "www.google.com", INTERNET_DEFAULT_HTTPS_PORT,
            NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0
        );
        if (!ggl_conn) {
            printf("Failed to connect: %lu\n", GetLastError());
            goto error;
        }

        static char const *accept[] = {
            "*/*", NULL
            //"text/html", "application/xhtml+xml", "application/xml;q=0.9", NULL
        };

        HINTERNET ggl_request = HttpOpenRequestA(
            ggl_conn, "GET", tmp, "HTTP/1.1",
            NULL, NULL, INTERNET_FLAG_SECURE, 0
        );
        if(!ggl_request) {
            printf("Failed to create(?) request %lu\n", GetLastError()) ;
            goto error;
        }

        bool ggl_sent = HttpSendRequestA(ggl_request, hdrs, STR_SIZE(hdrs), NULL, 0);
        if(!ggl_sent) {
            printf("Failed to send request %lu\n", GetLastError()) ;
            goto error;
        }

        static char const response_path[] = "./responses/page-";
        int const response_path_c = STR_SIZE(response_path);
        memcpy(tmp, response_path, response_path_c);
        static int page_i = 0;
        int extra_path_c = sprintf(tmp + response_path_c, "%d.txt", page_i);
        tmp[response_path_c + extra_path_c] = '\0';
        page_i++;

        FILE *file = fopen(tmp, "wb");

        DWORD bytesReadL;
        char *decode_end = page_tmp;
        char *page_end = page_tmp;
        int skip = STR_SIZE("<!doctype html><html lang=\"en\"><head>");
        int state = 0; // 0 - head, 1 - body
        while (InternetReadFile(ggl_request, page_end, page_tmp + page_tmp_size - page_end, &bytesReadL) && bytesReadL > 0) {
            int bytesRead = (int)bytesReadL;
            page_end += bytesRead;

            printf("Received %d bytes\n", bytesRead);

            while(!fwrite(decode_end, page_end - decode_end, 1, file));
            decode_end = page_end;
        }
        printf("Message sent %lu\n", bytesReadL);
        fclose(file);

        InternetCloseHandle(ggl_request);
        InternetCloseHandle(ggl_conn);

        //printf("Returning main page\n");
        //msg = page_msg;
        //msg_c = page_c;
        //int size = send(conn_socket, msg, msg_c, 0);
        //printf("Sent %d of %d\n", size, msg_c);
    }
    else if(received < 14 || client_request[4] == '/' && client_request[5] == ' ') {
        printf("Websockets? Surely...\n");
        static char const header_msg[]
            = "HTTP/1.1 101 Switching Protocols\r\n"
            "Connection: Upgrade\r\nUpgrade: websocket\r\n"
            "Sec-WebSocket-Accept: ";
        int const header_msg_c = STR_SIZE(header_msg);

        static char const b64lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        static char const key_header_name[] = "Sec-WebSocket-Key";
        int const key_header_c = STR_SIZE(key_header_name);

        char const *pos = strstr(client_request, key_header_name);
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

        char *msg = response;
        int msg_c = accept - response + 4;

        printf("Responding with %d bytes: `%.*s`\n", msg_c, msg_c, msg);

        int size = send(conn_socket, msg, msg_c, 0);
        websockets_sockets[websockets_c++] = conn_socket;
        printf("Sent %d of %d\n", size, msg_c);

        return;
    }
    else {
        printf("Not sending anything\n");
    }

error:
    closesocket(conn_socket);
#endif
}

static int setup_page(void) {
    FILE *file = fopen("src/test2.html", "rb");
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
    //bool it = argv[1][0] == 'b';
    bool it = true;
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

    printf("Initialised.\n");

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("Could not create socket: %d", WSAGetLastError());
        return 1;
    }

    struct sockaddr_in server = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = INADDR_ANY },
        .sin_port = htons(80),
    };

    if(!it) if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed with error code: %d", WSAGetLastError());
        return 1;
    }

    if(!it) if(setup_page()) {
        printf("No page!\n");
        return 1;
    }

    listen(server_socket, 60);

    fd_set read_fs;
    fd_set error_fs;


    if(it) {
        static wchar const object[] = L"search?q=js+find+min+element&asearch=arc"
            "&async=use_ac:true,_fmt:prog"
            ;
        int object_c = STR_SIZE(object);

        static wchar const hdrs[] = L"accept: */*\r\n"
         ;

        HINTERNET ggl_conn = WinHttpConnect(
            winhttp_state, L"www.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0
        );
        if (!ggl_conn) {
            printf("Failed to connect: %lu\n", GetLastError());
            return 1;
        }

        static wchar const *accept[] = {
            L"*/*", NULL
            //"text/html", "application/xhtml+xml", "application/xml;q=0.9", NULL
        };

        HINTERNET ggl_request = WinHttpOpenRequest(
            ggl_conn, NULL, object, NULL, L"https://www.google.com/", accept,
            WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH
        );
        if(!ggl_request) {
            printf("Failed to create(?) request %lu\n", GetLastError()) ;
            return 1;
        }

        bool ggl_sent = WinHttpSendRequest(
            ggl_request, hdrs, ARR_SIZE(hdrs) - 1,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0
        );
        if(!ggl_sent) {
            printf("Failed to send request %lu\n", GetLastError()) ;
            return 1;
        }

        bool ggl_recv = WinHttpReceiveResponse(ggl_request, NULL);
        if(!ggl_recv) {
            printf("Failed to receive request %lu\n", GetLastError()) ;
            return 1;
        }

        //DWORD headers_size = page_tmp_size;
        //if (!WinHttpQueryHeaders(
        //    ggl_request,
        //    WINHTTP_QUERY_RAW_HEADERS_CRLF,
        //    WINHTTP_HEADER_NAME_BY_INDEX,
        //    page_tmp, &headers_size,
        //    WINHTTP_NO_HEADER_INDEX
        //)) {
        //    printf("Headers? %ld\n", GetLastError());
        //    return 1;
        //}
        //
        //wprintf(L"Headers: `%.*s`\n", (int)headers_size, page_tmp);

        char *result_end = result_tmp;

        char *decode_end = page_tmp;
        char *recv_end = page_tmp;
        char *const buffer_end = page_tmp + page_tmp_size;

        static char const match_str[] = "<div jscontroller=\"SC7lYd\"";
        int match_size = STR_SIZE(match_str);

        int tag_end = 0;
        enum {
            SEARCH_NEXT, FIND_SEACH_END, PARSE_ANSWER
        } State;
        int decode_state = SEARCH_NEXT;

        while(true) {
            printf("Start!\n");
            DWORD dwSize = 0;
            if (!WinHttpQueryDataAvailable(ggl_request, &dwSize)) {
                printf("Some stupid error #1 %ld\n", GetLastError());
                return 1;
            }
            if(dwSize == 0) break;

            DWORD dwDownloaded = 0;
            int left = buffer_end - recv_end;
            if(!WinHttpReadData( // utf-8!
                ggl_request, recv_end,
                left, &dwDownloaded
            )) {
                printf("Some stupid error #2 %ld\n", GetLastError());
                return 1;
            }
            if(left == dwDownloaded) printf("Oui %d\n", left);
            if(dwDownloaded == 0) break;

            printf("Received %d bytes\n", (int)dwDownloaded);
            recv_end += dwDownloaded;

            next:
            switch(decode_state) {
                break; case SEARCH_NEXT: {
                    if(recv_end - decode_end < match_size) goto end;
                    int diff = get_diff(decode_end, match_str, match_size);
                    decode_end += MAX(diff, 1);
                    if(diff == match_size) {
                        decode_state = FIND_SEACH_END;
                        printf("Found\n");
                    }
                    goto next;
                }
                break; case FIND_SEACH_END: {
                    if(recv_end - decode_end < 1) goto end;
                    if(*decode_end == '>') {
                        tag_end = 0;
                        tag_stack[tag_end++] = ('d' << 16) | ('i' << 8) | ('v');
                        decode_state = PARSE_ANSWER;
                        printf("Found end\n");
                        printf("<div>\n");
                    }
                    decode_end++;
                    goto next;
                }
                break; case PARSE_ANSWER: {
                    // drop parse if not enough bytes. Should not be a big problem.
                    char *parse_end = decode_end;

                    if(recv_end - parse_end < 1) goto end;
                    bool match = *parse_end == '<';
                    parse_end++;
                    if(match) {
                        bool closing = false;
                        if(recv_end - parse_end < 1) goto end;
                        if(*parse_end == '/') {
                            closing = true;
                            parse_end++;
                        }

                        char *name_start = parse_end;

                        uint32_t name = 0;
                        while(true) {
                            if(recv_end - parse_end < 1) goto end;
                            char s = *parse_end;
                            if(s >= 'a' && s <= 'z') { // good enough
                                name = (name << 8) | (uint8_t)s;
                            } else {
                                break;
                            }
                            parse_end++;
                        }
                        int name_size = parse_end - name_start;

                        while(true) {
                            if(recv_end - parse_end < 1) goto end;
                            if(*parse_end == '>') break;
                            parse_end++;
                        }

                        if(closing) {
                            while(tag_end > 0) {
                                tag_end--;
                                if (tag_stack[tag_end] == name) {
                                    for(int i = 0; i < tag_end; i++) {
                                        printf("  ");
                                    }
                                    printf("</%.*s>\n", name_size, name_start);
                                    break;
                                }
                            }
                            if(tag_end == 0) {
                                decode_state = SEARCH_NEXT;
                                printf("Answer ended\n");
                            }
                        }
                        else {
                            for(int i = 0; i < tag_end; i++) {
                                printf("  ");
                            }
                            printf("<%.*s>\n", name_size, name_start);
                            tag_stack[tag_end++] = name;
                        }
                    }

                    decode_end = parse_end;
                    goto next;
                }
            }
            end:;
        }

        printf("Message sent %d\n", (int)(recv_end - page_tmp));

        return 0;
    }

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
        for(int i = 0; i < websockets_c; i++) {
            SOCKET *s = &websockets_sockets[i];
            if(FD_ISSET(*s, &read_fs)) {
                printf("Websocket received something!\n");

                int received = recv(*s, client_request, 2047, 0);
                if(received < 0) {
                    printf("Error receiving\n");
                    return 32;
                }

                if(received < 1 || client_request[0] != 0x81) {
                    // 8 for no continuation, 1 for text
                    printf("first byte %x is not 0x81\n", client_request[0]);
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
                for(int i = 0; i < len; i++) {
                    result_tmp[i] = client_msg[i] ^ mask[i % 4];
                }

                printf("Message: %.*s\n", len, result_tmp);

                printf("Request: `");
                for(int i = 0; i < received; i++) {
                    printf("\\%.2x", (uint8_t)client_request[i]);
                }
                printf("`\n");

                static char const msg[] = "Hello Client!";
                int const msg_c = STR_SIZE(msg);

                uint8_t *response = (uint8_t*)result_tmp;
                response[0] = (uint8_t)((1 << 7) | 1);
                response[1] = 13;
                memcpy(response + 2, msg, 2 + msg_c);
                int size = send(*s, (char*)response, 2 + msg_c, 0);
                printf("Sent %d: `%.*s`\n", size, msg_c, msg);
            }
        }
    }

    closesocket(server_socket);
    WSACleanup();

    return 0;
}
