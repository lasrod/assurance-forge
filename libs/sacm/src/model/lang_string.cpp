#include "sacm/model/lang_string.h"

namespace sacm::model {

const std::string* MultiLangString::find(std::string_view lang) const {
    for (const LangString& entry : values) {
        if (entry.lang == lang) {
            return &entry.content;
        }
    }
    return nullptr;
}

const std::string& MultiLangString::primary() const {
    static const std::string kEmpty;
    return values.empty() ? kEmpty : values.front().content;
}

void MultiLangString::set(std::string_view lang, std::string_view content) {
    for (LangString& entry : values) {
        if (entry.lang == lang) {
            entry.content = content;
            return;
        }
    }
    values.push_back(LangString{std::string(lang), std::string(content), std::nullopt});
}

}  // namespace sacm::model
