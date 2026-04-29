#pragma once

#include <string>
#include <vector>

namespace ai {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;
    long timeoutSeconds = 30;
};

struct HttpResponse {
    long statusCode = 0;
    std::string body;
    std::string errorMessage;
    bool networkError = false;
    bool timedOut = false;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse Post(const HttpRequest& request) = 0;
};

std::string RedactSensitiveText(const std::string& value);

}  // namespace ai
