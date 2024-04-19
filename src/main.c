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
#define STR_SIZE(str) (ARR_SIZE((str)) - 1)

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
    tag_stack_size = 256,
    result_tmp_size = 65536,
    page_tmp_size = 1024 * 1024,
};
static uint8_t client_request[2048] = "";
static uint32_t result_size;
static char result_tmp[result_tmp_size];
static int result_status; // 0 - not started, 1 - started, 2 - finished
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

static uint32_t make_tag_name(char *str) __attribute__((const));
static uint32_t make_tag_name(char *str) {
    uint32_t name = 0;
    while(*str) {
        name = (name << 8) | (uint8_t)(*str);
        str++;
    }
    return name;
}

static char const req_obj_search[] = "/search";
static char const req_obj_ws[] = "/ws";
static int const req_obj_search_size = STR_SIZE(req_obj_search);
static int const req_obj_ws_size = STR_SIZE(req_obj_ws);

typedef struct Result { bool err; } Result;
#define OK ((Result){ false })
#define ERR ((Result){ true })

static Result send_complete_(char const *msg, int size, SOCKET sock, char *file, int line) {
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
#define send_complete(msg, size, sock) send_complete_((msg), (size), (sock), __FILE__, __LINE__)

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
        if(result_status == 1) {
            printf("Concurrent request\n");
            goto error;
        }
        result_status = 1;

        // https://github.com/searxng/searxng/issues/159
        // Note: to filter out useful results, use <div jscontroller="SC7lYd"
        // (this if from SearXNG engines/google.py results_xpath)
        // Example: https://google.com/search?q=abc&asearch=arc&async=use_ac:true,_fmt:prog
        static wchar const object_str[]
            = L"/search?asearch=arc&async=use_ac:true,_fmt:prog&";
        int object_str_c = STR_SIZE(object_str);

        wchar *object = (wchar*)page_tmp;
        memcpy(object, object_str, sizeof(object_str));

        uint8_t *param_cur = req_obj_b + req_obj_search_size + 1;
        wchar_t *object_cur = object + object_str_c;
        while(param_cur != req_obj_e) *object_cur++ = *param_cur++;
        *object_cur = 0;
        wprintf(L"URL: %s\n", object);

#define FAKE_INET 0
#if !FAKE_INET
        static wchar const hdrs[] = L"accept: */*\r\n";

        HINTERNET ggl_conn = WinHttpConnect(
            winhttp_state, L"www.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0
        );
        if (!ggl_conn) {
            printf("Failed to connect: %lu\n", GetLastError());
            goto error;
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
            goto error;
        }

        bool ggl_sent = WinHttpSendRequest(
            ggl_request, hdrs, ARR_SIZE(hdrs) - 1,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0
        );
        if(!ggl_sent) {
            printf("Failed to send request %lu\n", GetLastError()) ;
            goto error;
        }

        // Send browser response page
        if(send_complete(page_msg, page_c, conn_socket).err) goto error;
        printf("Sent main page\n");

        bool ggl_recv = WinHttpReceiveResponse(ggl_request, NULL);
        if(!ggl_recv) {
            printf("Failed to receive request %lu\n", GetLastError()) ;
            goto error;
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
#else
        static char const path[] = "responses/page-0.txt";
        FILE *file = fopen(path, "rb");
#endif

        static char const out_path[] = "responses/response.html";
        FILE *out_file = fopen(out_path, "wb");

        char *result_end = result_tmp;

        char *ans_name_start = NULL;
        bool url_added = false;
        bool name_added = false;
        bool description_added = false;
        int outer_descr_level = -1;
        int desc_level = -1;
        char *desc_start = NULL;

        result_size = 0;

        char *decode_end = page_tmp;
        char *recv_end = page_tmp;
        char *const buffer_end = page_tmp + page_tmp_size;

        static char const match_str[] = "<div jscontroller=\"SC7lYd\"";
        int match_size = STR_SIZE(match_str);

        static char const href_str[] = "href=\"";
        int href_size = STR_SIZE(href_str);

        static char const desc_outer_str[] = "data-sncf=\""; // look further
        int desc_outer_str_size = STR_SIZE(desc_outer_str);

        int tag_end = 0;
        enum {
            SEARCH_NEXT, FIND_SEACH_END, PARSE_ANSWER
        } State;
        int decode_state = SEARCH_NEXT;

        while(true) {
#if !FAKE_INET
            printf("Start!\n");
            DWORD dwSize = 0;
            if (!WinHttpQueryDataAvailable(ggl_request, &dwSize)) {
                printf("Some stupid error #1 %ld\n", GetLastError());
                goto error;
            }
            if(dwSize == 0) break;
#endif

            DWORD dwDownloaded = 0;
            int left = buffer_end - recv_end;
#if !FAKE_INET
            if(!WinHttpReadData( // utf-8!
                ggl_request, recv_end,
                left, &dwDownloaded
            )) {
                printf("Some stupid error #2 %ld\n", GetLastError());
                goto error;
            }
#else
            dwDownloaded = fread(recv_end, 1, left, file);
#endif
            if(left == dwDownloaded) printf("Oui %d\n", left);
            if(dwDownloaded == 0) break;

            for(int i = 0; i < 10 && !fwrite(recv_end, dwDownloaded, 1, out_file); i++);

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
                        tag_stack[tag_end++] = make_tag_name("div");
                        decode_state = PARSE_ANSWER;
                        printf("Found end\n");
                        //printf("<div>\n");
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
                            if((s >= 'a' && s <= 'z') || (s >= '0' && s <= '9')) {
                                name = (name << 8) | (uint8_t)s;
                            } else {
                                break;
                            }
                            parse_end++;
                        }
                        int name_size = parse_end - name_start;

                        if(!closing && name == make_tag_name("div")) {
                            char *desc_parse_end = parse_end;

                            while(true) {
                                if(recv_end - desc_parse_end < desc_outer_str_size) {
                                    goto end;
                                }
                                if(*desc_parse_end == '>') goto not_desc;
                                int diff = get_diff(
                                    desc_parse_end, desc_outer_str,
                                    desc_outer_str_size
                                );
                                desc_parse_end += MAX(diff, 1);
                                if(diff == desc_outer_str_size) break;
                            }

                            outer_descr_level = tag_end;
                            parse_end = desc_parse_end;
                            not_desc:;
                        }
                        else if(!closing && name == 'a') {
                            char *a_parse_end = parse_end;
                            while(true) {
                                if(recv_end - a_parse_end < href_size) goto end;
                                if(*a_parse_end == '>') goto not_a;
                                int diff = get_diff(a_parse_end, href_str, href_size);
                                a_parse_end += MAX(diff, 1);
                                if(diff == href_size) break;
                            }

                            char *href_begin = a_parse_end;
                            while(true) {
                                if(recv_end - a_parse_end < 1) goto end;
                                if(*a_parse_end == '>') goto not_a;
                                if(*a_parse_end == '"') break;
                                a_parse_end++;
                            }
                            char *href_end = a_parse_end;
                            a_parse_end++;
                            parse_end = a_parse_end;

                            int href_size = (int)(href_end - href_begin);
                            if(!url_added) {
                                printf("URL added at %d\n", result_size);
                                url_added = true;
                                int href_size_b = MIN(href_size, 255);
                                *result_end++ = 1;
                                *result_end++ = href_size_b;
                                memcpy(result_end, href_begin, href_size_b);
                                result_end += href_size_b;
                                result_size = result_end - result_tmp;
                            }
                            printf("URL: %.*s\n", href_size, href_begin);
                            not_a:;
                        }

                        while(true) {
                            if(recv_end - parse_end < 1) goto end;
                            if(*parse_end++ == '>') break;
                        }

                        if(
                            !closing && outer_descr_level != -1
                            && tag_end == outer_descr_level + 1
                        ) {
                            outer_descr_level = -1;
                            desc_level = tag_end;
                            desc_start = parse_end;
                        }
                        else if(!closing && name == make_tag_name("h3")) {
                            ans_name_start = parse_end;
                        }
                        else if(closing && name == make_tag_name("h3")) {
                            int size = (int)(decode_end - ans_name_start);
                            if(!name_added) {
                                name_added = true;
                                int size_b = MIN(size, 255);
                                *result_end++ = 2;
                                *result_end++ = size;
                                memcpy(result_end, ans_name_start, size_b);
                                result_end += size_b;
                                result_size = result_end - result_tmp;
                            }
                            printf("Name: %.*s\n", size, ans_name_start);
                            ans_name_start = NULL;
                        }

                        if(name == make_tag_name("br")) {
                            //for(int i = 0; i < tag_end; i++) {
                            //    printf("  ");
                            //}
                            //printf("<%.*s>\n", name_size, name_start);
                        }
                        else if(closing) {
                            while(tag_end > 0) {
                                tag_end--;
                                if (tag_stack[tag_end] == name) {
                                    //for(int i = 0; i < tag_end; i++) {
                                    //    printf("  ");
                                    //}
                                    //printf("</%.*s>\n", name_size, name_start);
                                    break;
                                }
                            }

                            if(outer_descr_level >= tag_end) {
                                printf("Desc none!\n");
                                outer_descr_level = -1;
                            }
                            else if(desc_level >= tag_end) {
                                int size = (int)(decode_end - desc_start);
                                if(!description_added) {
                                    description_added = true;
                                    int size_b = MIN(size, 255);
                                    *result_end++ = 3;
                                    *result_end++ = size;
                                    memcpy(result_end, desc_start, size_b);
                                    result_end += size_b;
                                    result_size = result_end - result_tmp;
                                }
                                printf("Desc: %.*s\n", size, desc_start);
                                desc_level = -1;
                            }

                            if(tag_end == 0) {
                                ans_name_start = NULL;
                                url_added = false;
                                name_added = false;
                                description_added = false;
                                outer_descr_level = -1;
                                desc_level = -1;
                                desc_start = NULL;

                                *result_end++ = 4;
                                result_size = result_end - result_tmp;
                                decode_state = SEARCH_NEXT;
                                printf("Answer ended\n");
                            }
                        }
                        else {
                            //for(int i = 0; i < tag_end; i++) {
                            //    printf("  ");
                            //}
                            //printf("<%.*s>\n", name_size, name_start);
                            tag_stack[tag_end++] = name;
                        }
                    }

                    decode_end = parse_end;
                    goto next;
                }
            }
            end:;
        }

        fclose(out_file);
        printf("Result is %d bytes!\n", result_size);
        result_status = 2;

        WinHttpCloseHandle(ggl_conn);
        WinHttpCloseHandle(ggl_request);
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

        uint8_t *digest = (uint8_t*)page_tmp;
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
        websock_response[1] = 126;
        assert(result_size < 65536);
        websock_response[2] = (uint8_t)(result_size >> 8);
        websock_response[3] = (uint8_t)result_size;
        memcpy(websock_response + 4, result_tmp, result_size);
        int websock_response_size = 4 + result_size;

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
    // hopefully this is not a query connection
    // as this will deadlock everything with result_status == 1
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
            uint8_t *response = (uint8_t*)page_tmp;
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

    closesocket(server_socket);
    WSACleanup();

    return 0;
}
