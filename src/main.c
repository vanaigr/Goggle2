//#include<windows.h>
//#include<wininet.h>
#include<winsock2.h>
#include<stdio.h>

#include<stdint.h>
#include<stdbool.h>

//#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

#define ARR_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

int main(void) {
WSADATA wsa;
    SOCKET s;

    printf("\nInitialising Winsock...");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code : %d", WSAGetLastError());
        return 1;
    }

    printf("Initialised.\n");

    // Create a socket
    if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
        printf("Could not create socket : %d", WSAGetLastError());
    }

    printf("Socket created.\n");

    // Prepare the sockaddr_in structure
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(1619);

    // Bind
    if (bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed with error code : %d", WSAGetLastError());
        return 1;
    }
    printf("Bind done\n");

    static const char page_msg[]
        =
        "HTTP/1.0 200 OK\r\n"
        "Content-type: text/html\r\n"
        //"Content-length: 96\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head><title>Test!</title></head><body><h1>Hello, World!</h1></body></html>";
    int const page_c = ARR_SIZE(page_msg);

    static char const not_found_msg[] = "HTTP/1.0 404 Not Found\r\n\r\n";
    int const not_found_c = ARR_SIZE(not_found_msg);
    static char const not_impl_msg[] = "HTTP/1.0 501 Not Implemented\r\n\r\n";
    int const not_impl_c = ARR_SIZE(not_impl_msg);

    static char client_request[2048];

    listen(s, 3);

    while(true) {
        printf("Waiting for incoming connections...\n");

        SOCKET conn_socket = accept(s, NULL, NULL);
        if (conn_socket == INVALID_SOCKET) {
            printf("Accept failed with error code : %d", WSAGetLastError());
            continue;
        }

        int received = recv(conn_socket, client_request, 2047, 0);
        printf("Received %d bytes: `%s`", received, client_request);
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
        else if(received < 14 || memcmp(client_request + 4, search, 10) != 0) {
            printf("Response is asking for something oth\n");
            msg = not_found_msg;
            msg_c = not_impl_c;
        }
        else {
            printf("Returning main page\n");
            msg = page_msg;
            msg_c = page_c;
        }

        printf("Connection accepted\n");
        int size = send(conn_socket, msg, msg_c, 0);
        shutdown(conn_socket, SB_BOTH);
        printf("Sent %d of %d\n", size, msg_c);
        closesocket(conn_socket);
    }

    closesocket(s);
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
