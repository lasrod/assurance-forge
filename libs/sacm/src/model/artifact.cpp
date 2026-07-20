#include "sacm/model/artifact.h"

namespace sacm::model {

// Property is complete in this translation unit, so the constructor's
// exception-cleanup path and the destructor both instantiate the
// vector<unique_ptr<Property>> member operations cleanly here. Both are
// declared in the header to keep those instantiations out of translation
// units where Property is only forward-declared.
ArtifactAsset::ArtifactAsset(ElementKind kind, ElementId id)
    : ArtifactElement(kind, std::move(id)) {}

ArtifactAsset::~ArtifactAsset() = default;

}  // namespace sacm::model
