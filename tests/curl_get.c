/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* curl_get <url>: one GET through libcurl + mbedTLS with the shipped CA bundle;
 * prints the status line, response code, bytes received and the TLS peer's
 * verification result. The first check that DNS, sockets, TLS and the clock
 * work on the target, before any browser code exists. */
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static size_t sink(char *p, size_t s, size_t n, void *u) { (*(size_t *)u) += s * n; return s * n; }

int main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : "https://example.com/";
    size_t got = 0;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    printf("libcurl %s, ssl %s, protocols:", v->version, v->ssl_version ? v->ssl_version : "none");
    for (const char *const *p = v->protocols; *p; p++) printf(" %s", *p);
    printf("\n");
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Florence-test/0");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &got);
    CURLcode rc = curl_easy_perform(c);
    long code = 0, verify = -1;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(c, CURLINFO_SSL_VERIFYRESULT, &verify);
    printf("%s: rc=%d (%s) http=%ld bytes=%zu ssl_verify=%ld\n", url, (int)rc, curl_easy_strerror(rc), code, got, verify);
    curl_easy_cleanup(c);
    curl_global_cleanup();
    return rc == CURLE_OK ? 0 : 1;
}
