#include "ai/http_client.h"

#include <algorithm>

namespace ai {

std::string RedactSensitiveText(const std::string& value) {
    std::string redacted = value;
    const std::string bearer = "Bearer ";
    size_t pos = redacted.find(bearer);
    while (pos != std::string::npos) {
        size_t start = pos + bearer.size();
        size_t end = redacted.find_first_of("\r\n \t", start);
        if (end == std::string::npos) end = redacted.size();
        redacted.replace(start, end - start, "[redacted]");
        pos = redacted.find(bearer, start + 10);
    }
    return redacted;
}

}  // namespace ai