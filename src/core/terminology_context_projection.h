#pragma once

#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace core {

// Refresh the parser-side name/description projection for every visible
// terminology artifact reference in the package. Returns true if any
// element changed. Pure: depends only on the supplied model and package.
bool RefreshVisibleTerminologyContextProjection(parser::AssuranceCase& model,
                                                const sacm::AssuranceCasePackage& package);

// After an "add as visible context" mutation, ensure the parser model
// contains placeholder elements for the new ArtifactReference and
// AssertedContext, then refresh the projection. Returns true if the
// parser model changed.
bool SyncVisibleTerminologyContextToParser(parser::AssuranceCase& model,
                                           const sacm::AssuranceCasePackage& package,
                                           const TerminologyContextAssociationResult& result);

} // namespace core
