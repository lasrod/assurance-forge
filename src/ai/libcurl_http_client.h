#pragma once

#include "ai/http_client.h"

namespace ai {

class LibCurlHttpClient final : public IHttpClient {
public:
    HttpResponse Post(const HttpRequest& request) override;
};

}  // namespace ai
