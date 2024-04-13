#include<stdio.h>
#include<winsock2.h>
#include<stdint.h>

#pragma comment(lib, "ws2_32.lib")

int const RESP_SIZE = 20000;

int main(void) {
    WSADATA wsaData;
    SOCKET s;
    struct sockaddr_in server;
    char *message, server_reply[RESP_SIZE];
    int recv_size;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
        return 1;
    }

    // Create socket
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Could not create socket.\n");
        return 1;
    }

    printf("Socket created.\n");

    uint32_t addr = (142u) | (251u << 8) | (46u << 16) | (174u << 24);

    // Set up the server address
    server.sin_addr.s_addr = addr;
    server.sin_family = AF_INET;
    server.sin_port = htons(80); // HTTP port

    // Connect to server
    if (connect(s, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Connect failed.\n");
        return 1;
    }

    printf("Connected.\n");

    // Send HTTP request
    message = "GET /search?q=bob HTTP/1.1\r\nHost: www.google.com\r\n\r\n";
    if (send(s, message, strlen(message), 0) < 0) {
        printf("Send failed.\n");
        return 1;
    }
    printf("HTTP request sent.\n");

    // Receive a response from the server
    if ((recv_size = recv(s, server_reply, RESP_SIZE, 0)) == SOCKET_ERROR) {
        printf("Receive failed.\n");
    }

    // Print the response
    printf("Server reply:\n");
    printf("%.*s", recv_size, server_reply);

    closesocket(s);
    WSACleanup();

    return 0;
}
