
#include<windows.h>
#include<winhttp.h>

#include<stdio.h>

#include"common.h"

static uint32_t make_tag_name(char *str) __attribute__((const));
static uint32_t make_tag_name(char *str) {
    uint32_t name = 0;
    while(*str) {
        name = (name << 8) | (uint8_t)(*str);
        str++;
    }
    return name;
}

#if 0
#   define OUT_OPEN(path, mod) (fopen((path), (mod)))
    void OUT_WRITE(uint8_t *buf, int size, FILE *file) {
        for(int i = 0; i < 10 && !fwrite(buf, size, 1, file); i++);
    }
#   define OUT_CLOSE(file) (fclose((file))
#else
#   define OUT_OPEN(path, mod) NULL
#   define OUT_WRITE(buf, size, file)
#   define OUT_CLOSE(file)
#endif

#if 0
#   define TREE_PRINTF(...) (printf(__VA_ARGS__))
#else
#   define TREE_PRINTF(...)
#endif

uint32_t extract(
    SOCKET sock, HINTERNET ggl_conn,
    char const *params_b, char const *params_e,
    uint32_t *const tmp, uint32_t *const tmp_end,
    uint8_t *const result_b, uint8_t *const result_e
) {
    // https://github.com/searxng/searxng/issues/159
    // Note: to filter out useful results, use <div jscontroller="SC7lYd"
    // (this if from SearXNG engines/google.py results_xpath)
    // Example: https://google.com/search?q=abc&asearch=arc&async=use_ac:true,_fmt:prog
    static wchar const object_str[]
        = L"/search?asearch=arc&async=use_ac:true,_fmt:prog&";
    int object_str_c = STR_SIZE(object_str);

    wchar *object = (wchar*)tmp;
    memcpy(object, object_str, sizeof(object_str));

    char const *param_cur = params_b;
    wchar_t *object_cur = object + object_str_c;
    while(param_cur != params_e) *object_cur++ = *param_cur++;
    *object_cur = 0;
    wprintf(L"URL: %s\n", object);

#define FAKE_INET 0
#if !FAKE_INET
    static wchar const hdrs[] = L"accept: */*\r\n";

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
    if(send_main_page(sock).err) goto error;
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
    FILE *out_file = OUT_OPEN(out_path, "wb");

    uint8_t *ans_name_start = NULL;
    bool url_added = false;
    bool name_added = false;
    bool description_added = false;
    int outer_descr_level = -1;
    int desc_level = -1;
    uint8_t *desc_start = NULL;


    uint32_t *const tag_stack_b = tmp;
    uint32_t *const tag_stack_e = tmp + 256;
    uint8_t *const buffer_end = (uint8_t*)tmp_end;

    uint8_t *recv_end = (uint8_t*)tag_stack_e;
    uint8_t *decode_end = recv_end;
    uint8_t *result_end = result_b;

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

        OUT_WRITE(recv_end, dwDownloaded, out_file);

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
                    TREE_PRINTF("Found\n");
                }
                goto next;
            }
            break; case FIND_SEACH_END: {
                if(recv_end - decode_end < 1) goto end;
                if(*decode_end == '>') {
                    tag_end = 0;
                    tag_stack_b[tag_end++] = make_tag_name("div");
                    decode_state = PARSE_ANSWER;
                    TREE_PRINTF("Found end\n");
                    //TREE_PRINTF("<div>\n");
                }
                decode_end++;
                goto next;
            }
            break; case PARSE_ANSWER: {
                // drop parse if not enough bytes. Should not be a big problem.
                uint8_t *parse_end = decode_end;

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

                    uint8_t *name_start = parse_end;

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
                        uint8_t *desc_parse_end = parse_end;

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
                        uint8_t *a_parse_end = parse_end;
                        while(true) {
                            if(recv_end - a_parse_end < href_size) goto end;
                            if(*a_parse_end == '>') goto not_a;
                            int diff = get_diff(a_parse_end, href_str, href_size);
                            a_parse_end += MAX(diff, 1);
                            if(diff == href_size) break;
                        }

                        uint8_t *href_begin = a_parse_end;
                        while(true) {
                            if(recv_end - a_parse_end < 1) goto end;
                            if(*a_parse_end == '>') goto not_a;
                            if(*a_parse_end == '"') break;
                            a_parse_end++;
                        }
                        uint8_t *href_end = a_parse_end;
                        a_parse_end++;
                        parse_end = a_parse_end;

                        int href_size = (int)(href_end - href_begin);
                        if(!url_added) {
                            url_added = true;
                            int href_size_b = MIN(href_size, 255);
                            *result_end++ = 1;
                            *result_end++ = href_size_b;
                            memcpy(result_end, href_begin, href_size_b);
                            result_end += href_size_b;
                        }
                        TREE_PRINTF("URL: %.*s\n", href_size, href_begin);
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
                        }
                        TREE_PRINTF("Name: %.*s\n", size, ans_name_start);
                        ans_name_start = NULL;
                    }

                    if(name == make_tag_name("br")) {
                        //for(int i = 0; i < tag_end; i++) {
                        //    TREE_PRINTF("  ");
                        //}
                        //TREE_PRINTF("<%.*s>\n", name_size, name_start);
                    }
                    else if(closing) {
                        while(tag_end > 0) {
                            tag_end--;
                            if (tag_stack_b[tag_end] == name) {
                                //for(int i = 0; i < tag_end; i++) {
                                //    TREE_PRINTF("  ");
                                //}
                                //TREE_PRINTF("</%.*s>\n", name_size, name_start);
                                break;
                            }
                        }

                        if(outer_descr_level >= tag_end) {
                            TREE_PRINTF("Desc none!\n");
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
                            }
                            TREE_PRINTF("Desc: %.*s\n", size, desc_start);
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
                            decode_state = SEARCH_NEXT;
                            TREE_PRINTF("Answer ended\n");
                        }
                    }
                    else {
                        //for(int i = 0; i < tag_end; i++) {
                        //    TREE_PRINTF("  ");
                        //}
                        //TREE_PRINTF("<%.*s>\n", name_size, name_start);
                        tag_stack_b[tag_end++] = name;
                    }
                }

                decode_end = parse_end;
                goto next;
            }
        }
        end:;
    }

    OUT_CLOSE(out_file);

    uint32_t result_size = result_end - result_b;
    printf("Result is %d bytes!\n", result_size);

    WinHttpCloseHandle(ggl_request);

    return result_size;
error:
    WinHttpCloseHandle(ggl_request);
    return -1;
}
