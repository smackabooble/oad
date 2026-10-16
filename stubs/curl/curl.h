// Minimal libcurl stand-in for the browser build: mod downloads and user reports are unavailable,
// every operation fails cleanly.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CURL_ CURL;
typedef struct CURLM_ CURLM;
typedef int64_t curl_off_t;
typedef int CURLcode;
typedef int CURLMcode;
typedef int CURLoption;
typedef int CURLINFO;
enum { CURLE_OK = 0, CURLE_COULDNT_CONNECT = 7, CURLE_UNSUPPORTED_PROTOCOL = 1 };
enum { CURLM_OK = 0, CURLM_BAD_HANDLE = 1, CURLM_BAD_EASY_HANDLE = 2 };
enum { CURLMSG_DONE = 1 };
enum { CURL_ERROR_SIZE = 256 };
#define CURL_READFUNC_ABORT 0x10000000
#define CURL_GLOBAL_ALL 3
#define CURL_GLOBAL_WIN32 2
#define CURL_GLOBAL_SSL 1
enum {
	CURLOPT_URL = 10002, CURLOPT_WRITEFUNCTION = 20011, CURLOPT_WRITEDATA = 10001, CURLOPT_READFUNCTION = 20012,
	CURLOPT_READDATA = 10009, CURLOPT_HTTPHEADER = 10023, CURLOPT_ERRORBUFFER = 10010, CURLOPT_FOLLOWLOCATION = 52,
	CURLOPT_NOSIGNAL = 99, CURLOPT_NOPROGRESS = 43, CURLOPT_XFERINFOFUNCTION = 20219, CURLOPT_XFERINFODATA = 10057,
	CURLOPT_VERBOSE = 41, CURLOPT_PROXY = 10004, CURLOPT_POSTFIELDSIZE_LARGE = 30120, CURLOPT_POST = 47,
	CURLOPT_MAXREDIRS = 68, CURLOPT_FAILONERROR = 45, CURLOPT_CONNECTTIMEOUT = 78, CURLOPT_USERAGENT = 10018,
	CURLOPT_POSTFIELDS = 10015, CURLOPT_TIMEOUT = 13, CURLOPT_UPLOAD = 46, CURLOPT_INFILESIZE_LARGE = 30115
};
enum { CURLINFO_RESPONSE_CODE = 0x200002 };
struct curl_slist { char* data; struct curl_slist* next; };
typedef struct CURLMsg {
	int msg; CURL* easy_handle;
	union { void* whatever; CURLcode result; } data;
} CURLMsg;
CURLcode curl_global_init(long flags);
void curl_global_cleanup(void);
CURL* curl_easy_init(void);
void curl_easy_cleanup(CURL*);
CURLcode curl_easy_setopt(CURL*, CURLoption, ...);
CURLcode curl_easy_perform(CURL*);
CURLcode curl_easy_getinfo(CURL*, CURLINFO, ...);
const char* curl_easy_strerror(CURLcode);
char* curl_easy_escape(CURL*, const char*, int);
void curl_free(void*);
struct curl_slist* curl_slist_append(struct curl_slist*, const char*);
void curl_slist_free_all(struct curl_slist*);
const char* curl_version(void);
#define CURLVERSION_NOW 4
typedef struct { int age; const char* version; unsigned int version_num; } curl_version_info_data;
const curl_version_info_data* curl_version_info(int);
CURLM* curl_multi_init(void);
CURLMcode curl_multi_cleanup(CURLM*);
CURLMcode curl_multi_add_handle(CURLM*, CURL*);
CURLMcode curl_multi_remove_handle(CURLM*, CURL*);
CURLMcode curl_multi_perform(CURLM*, int*);
CURLMsg* curl_multi_info_read(CURLM*, int*);
const char* curl_multi_strerror(CURLMcode);
#ifdef __cplusplus
}
#endif
