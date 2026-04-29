#include "ai/libcurl_http_client.h"

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <utility>

namespace ai {
namespace {

size_t WriteBody(char* data, size_t size, size_t count, void* user_data) {
    auto* body = static_cast<std::string*>(user_data);
    body->append(data, size * count);
    return size * count;
}

void EnsureCurlInitialized() {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

}  // namespace

HttpResponse LibCurlHttpClient::Post(const HttpRequest& request) {
    EnsureCurlInitialized();

    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.networkError = true;
        response.errorMessage = "Could not initialize HTTP client.";
        return response;
    }

    curl_slist* headers = nullptr;
    for (const auto& header : request.headers) {
        std::string line = header.name + ": " + header.value;
        headers = curl_slist_append(headers, line.c_str());
    }

    char error_buffer[CURL_ERROR_SIZE] = {};
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    response.body = std::move(body);

    if (code != CURLE_OK) {
        response.timedOut = code == CURLE_OPERATION_TIMEDOUT;
        response.networkError = !response.timedOut;
        response.errorMessage = error_buffer[0] ? RedactSensitiveText(error_buffer) : curl_easy_strerror(code);
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace ai
