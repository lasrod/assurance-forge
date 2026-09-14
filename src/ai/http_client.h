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
    long timeoutSeconds = 120;
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
    // Reading, not inference. A provider publishes which models an account may
    // use, and a caller choosing a model should read that list rather than
    // guess a name -- a wrong guess is a paid request that fails, or worse, a
    // silently different model behind a familiar name. `request.body` is
    // ignored.
    virtual HttpResponse Get(const HttpRequest& request) = 0;
};

std::string RedactSensitiveText(const std::string& value);

} // namespace ai
