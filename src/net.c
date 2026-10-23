#include "app.h"
#include <winhttp.h>
#include <stdlib.h>

#define UA L"PrayerReminder/1.0 (Windows)"

char *net_get(const wchar_t *host, const wchar_t *path, int *outLen)
{
    HINTERNET ses = NULL, con = NULL, req = NULL;
    char  *buf = NULL;
    DWORD  cap = 0, len = 0;

    if (outLen) *outLen = 0;

    ses = WinHttpOpen(UA, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) goto done;
    WinHttpSetTimeouts(ses, 8000, 8000, 15000, 15000);

    con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) goto done;

    req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) goto done;

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto done;
    if (!WinHttpReceiveResponse(req, NULL)) goto done;

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) goto done;

    cap = 64 * 1024;
    buf = (char *)malloc(cap);
    if (!buf) goto done;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { free(buf); buf = NULL; goto done; }
        if (avail == 0) break;
        if (len + avail + 1 > cap) {
            while (len + avail + 1 > cap) cap *= 2;
            if (cap > 8u * 1024 * 1024) { free(buf); buf = NULL; goto done; }
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); buf = NULL; goto done; }
            buf = nb;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, buf + len, avail, &got)) { free(buf); buf = NULL; goto done; }
        if (got == 0) break;
        len += got;
    }
    buf[len] = 0;
    if (outLen) *outLen = (int)len;

done:
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return buf;
}
