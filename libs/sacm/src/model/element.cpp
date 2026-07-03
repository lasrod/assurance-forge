#include "sacm/model/element.h"

namespace sacm::model {

const MultiLangString& ModelElement::description() const {
    static const MultiLangString kEmpty;
    return descriptions_.empty() ? kEmpty : descriptions_.front()->content();
}

}  // namespace sacm::model
