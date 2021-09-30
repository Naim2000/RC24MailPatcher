#include "cacert_pem.h"
#include <curl/curl.h>
#include <gccore.h>
#include <assert.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>
#include "patcher.h"

// Override CA certificate with those bundled from cacert.pem.
// A message from Spotlight: TODO: This is very broken.
static CURLcode ssl_ctx_callback(CURL *curl, void *ssl_ctx, void *userptr) {
    mbedtls_ssl_config *config = (mbedtls_ssl_config *)ssl_ctx;
    mbedtls_ssl_conf_ca_chain(config, (mbedtls_x509_crt *)userptr, NULL);

    return CURLE_OK;
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb,
                                  void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 1;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

s32 post_request(char *url, char *post_arguments, char **response) {
    CURL *curl;
    CURLcode res;

    struct MemoryStruct chunk;
    chunk.memory = (char *)malloc(4);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    struct curl_slist *headers=NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "WiiPatcher/1.0 (Nintendo Wii)");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_arguments);

    // Set CA certificate
    static mbedtls_x509_crt cacert;
    mbedtls_x509_crt_init(&cacert);
    s32 ret = mbedtls_x509_crt_parse(
    &cacert, (const unsigned char *)&cacert_pem, cacert_pem_size);
    if (ret < 0) {
        printf(":-----------------------------------------:\n");
        printf("Setting SSL certificate failed: %d\n", ret);
        printf(":-----------------------------------------:\n\n");
        return ret;
    }

    curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, &cacert);
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_callback);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        return (s32)res;
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    curl_global_cleanup();

    *response = chunk.memory;
    return 0;
}
