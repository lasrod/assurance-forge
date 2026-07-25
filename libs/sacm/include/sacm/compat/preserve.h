#pragma once

#include "sacm/model/document.h"

#include <cstddef>

namespace sacm::compat {

// Copies compatibility content from `source` onto `target`: each element's
// preserved child elements and preserved vendor attributes, matched by element
// id; the document-level foreign namespace declarations those fragments depend
// on to stay readable; and the preserved-element ids that let a reference into
// compatibility content be reported as untyped (SACM-REF-003) rather than
// missing (SACM-REF-001). Restoring the bytes without the ids would leave the
// rebuilt document reporting a hard error against content its own output still
// carries.
//
// Why this exists. A client that rebuilds a document from a lossy intermediate
// -- a projection, an application's own structs, anything with no field for
// unknown XML -- loses everything a tolerant load preserved, even though the
// content is still sitting in the document it rebuilt FROM. Round-tripping
// through such an intermediate is a legitimate thing for a client to do (it is
// how an editor reproduces a legacy mutation), but silently dropping vendor
// content while doing it is not. This lets the client put back what its
// intermediate could not carry, without needing to understand the content.
//
// Only elements present in BOTH documents receive content; an element that the
// rebuild dropped has nowhere to put it, and one the rebuild invented has
// nothing to receive. Existing content on `target` is not replaced -- an
// element that already carries preserved content keeps it, since that content
// came from the more recent parse.
//
// Returns the number of elements that received content.
std::size_t adopt_preserved_content(model::Document& target, const model::Document& source);

}  // namespace sacm::compat
