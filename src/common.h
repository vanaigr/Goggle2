#pragma once

#include<windows.h>
#include<winhttp.h>

#include<stdint.h>
#include<stdbool.h>

typedef unsigned short wchar;

#define ARR_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))
#define STR_SIZE(str) (ARR_SIZE((str)) - 1)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct { bool err; } Result;
#define OK ((Result){ false })
#define ERR ((Result){ true })

Result send_complete_(char const *msg, int size, SOCKET sock, char *file, int line);
#define send_complete(msg, size, sock) send_complete_((msg), (size), (sock), __FILE__, __LINE__)


uint32_t extract(
    SOCKET sock, HINTERNET ggl_conn,
    char const *params_b, char const *params_e,
    uint32_t *const tmp, uint32_t *const tmp_end,
    uint8_t *const result_b, uint8_t *const result_e
);

Result send_main_page(SOCKET sock);

inline int get_diff(char const *b1, char const *b2, int len) {
    for(int i = 0; i < len; i++) {
        if(b1[i] != b2[i]) return i;
    }
    return len;
}
