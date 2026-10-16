#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
CURLcode curl_global_init(long) { return CURLE_OK; }
void curl_global_cleanup(void) {}
CURL* curl_easy_init(void) { return (CURL*)calloc(1, 1); }
void curl_easy_cleanup(CURL* c) { free(c); }
CURLcode curl_easy_setopt(CURL*, CURLoption, ...) { return CURLE_OK; }
CURLcode curl_easy_perform(CURL*) { return CURLE_COULDNT_CONNECT; }
CURLcode curl_easy_getinfo(CURL*, CURLINFO, ...) { return CURLE_UNSUPPORTED_PROTOCOL; }
const char* curl_easy_strerror(CURLcode) { return "networking is not available in the browser build"; }
char* curl_easy_escape(CURL*, const char* s, int n) { size_t l = n ? (size_t)n : strlen(s); char* r = (char*)malloc(l + 1); memcpy(r, s, l); r[l] = 0; return r; }
void curl_free(void* p) { free(p); }
struct curl_slist* curl_slist_append(struct curl_slist* l, const char* s) {
	curl_slist* n = (curl_slist*)malloc(sizeof(curl_slist)); n->data = strdup(s); n->next = nullptr;
	if (!l) return n; curl_slist* t = l; while (t->next) t = t->next; t->next = n; return l;
}
void curl_slist_free_all(struct curl_slist* l) { while (l) { curl_slist* n = l->next; free(l->data); free(l); l = n; } }
const char* curl_version(void) { return "stub"; }
const curl_version_info_data* curl_version_info(int) { static const curl_version_info_data d = { 4, "stub", 0x070000 }; return &d; }
CURLM* curl_multi_init(void) { return (CURLM*)calloc(1, 1); }
CURLMcode curl_multi_cleanup(CURLM* m) { free(m); return CURLM_OK; }
CURLMcode curl_multi_add_handle(CURLM*, CURL*) { return CURLM_OK; }
CURLMcode curl_multi_remove_handle(CURLM*, CURL*) { return CURLM_OK; }
CURLMcode curl_multi_perform(CURLM*, int* running) { *running = 0; return CURLM_OK; }
CURLMsg* curl_multi_info_read(CURLM*, int* left) { *left = 0; return nullptr; }
const char* curl_multi_strerror(CURLMcode) { return "unavailable"; }
