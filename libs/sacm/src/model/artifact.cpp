#include "sacm/model/artifact.h"

namespace sacm::model {

// Property is complete here, so the implicit destruction of the
// vector<unique_ptr<Property>> member instantiates cleanly. Declared in the
// header to keep it out of translation units where Property is incomplete.
ArtifactAsset::~ArtifactAsset() = default;

}  // namespace sacm::model
