#include<assert.h>
#include<stdio.h>
#include<string.h>

#include<stdint.h>
#include<stdbool.h>

//#include<windows.h>
//#include<wininet.h>
#include<winsock2.h>

#include<sha1/sha1.h>

//#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

#define ARR_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))
#define STR_SIZE(str) (ARR_SIZE(str) - 1)

static const char page_header_msg[]
    = "HTTP/1.1 200 OK\r\n"
    "Content-type: text/html\r\n"
    "\r\n";
int const page_header_c = STR_SIZE(page_header_msg);
static char const not_found_msg[] = "HTTP/1.1 404 Not Found\r\n\r\n";
int const not_found_c = STR_SIZE(not_found_msg);
static char const not_impl_msg[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
int const not_impl_c = STR_SIZE(not_impl_msg);

static uint8_t client_request[2048] = "";
static char tmp[2048];
static char *page_msg;
static int page_c;

static SOCKET server_socket;
static SOCKET websockets_sockets[64];
static int websockets_c = 0;

static void handle_server_socket(void) {
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

    static char const get[] = "GET";
    static char const search[] = "/search?q=";

    char const *msg;
    int msg_c;
    if(received < 3 || memcmp(client_request, get, 3) != 0) {
        printf("Response not Implemented\n");
        msg = not_impl_msg;
        msg_c = not_impl_c;
    }
    else if(received >= 14 && memcmp(client_request + 4, search, 10) == 0) {
        printf("Returning main page\n");
        msg = page_msg;
        msg_c = page_c;
        int size = send(conn_socket, msg, msg_c, 0);
        printf("Sent %d of %d\n", size, msg_c);
    }
    else if(received < 14 || client_request[4] == '/' && client_request[5] == ' ') {
        printf("Websockets? Surely...\n");
        static char const header_msg[]
            = "HTTP/1.1 101 Switching Protocols\r\n"
            "Connection: Upgrade\r\nUpgrade: websocket\r\n"
            "Sec-WebSocket-Accept: ";
        //static char const header_msg2[]
        //    = "\r\nSec-WebSocket-Protocol: chat\r\n\r\n";
        int const header_msg_c = STR_SIZE(header_msg);
        //int const header_msg2_c = STR_SIZE(header_msg2);

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
        //memcpy(accept, header_msg2, header_msg2_c);

        msg = response;
        msg_c = accept - response + 4;// + header_msg2_c;

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

int main(void) {
    WSADATA wsa;

    printf("\nInitialising Winsock...");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code: %d", WSAGetLastError());
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
                    tmp[i] = client_msg[i] ^ mask[i % 4];
                }

                printf("Message: %.*s\n", len, tmp);

                printf("Request: `");
                for(int i = 0; i < received; i++) {
                    printf("\\%.2x", (uint8_t)client_request[i]);
                }
                printf("`\n");

                static char const msg[] = "Hello Client!";
                int const msg_c = STR_SIZE(msg);

                uint8_t *response = (uint8_t*)tmp;
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

#if 0
    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    // Initialize WinINet
    hInternet = InternetOpenA("HTTP GET", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        printf("Failed to initialize WinINet: %lu\n", GetLastError());
        return 1;
    }

    // Connect to the server
    hConnect = InternetOpenUrlA(hInternet, "https://google.com/search?q=bob", NULL, 0, INTERNET_FLAG_SECURE, 0);
    if (!hConnect) {
        printf("Failed to connect: %lu\n", GetLastError());
        InternetCloseHandle(hInternet);
        return 1;
    }

    // Send the GET request
    char buffer[4096];
    DWORD bytesRead;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        // Output received data
        fwrite(buffer, 1, bytesRead, stdout);
    }

    // Cleanup
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return 0;
}
#endif
