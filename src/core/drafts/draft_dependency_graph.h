#pragma once

// Which draft change groups cannot be accepted without which others.
//
// The unit a user accepts is a group, but groups are not independent. An SCCG
// review can reword an element an MCP client created; that element does not
// exist in the accepted argument at all, so accepting the rewording alone is not
// a smaller change, it is an impossible one. A new argument branch is the same
// case in reverse: accepting the claim without the strategy and the
// relationships that carry it produces an argument that asserts support it has
// not got.
//
// So dependencies are **inferred and stored**, and the UI shows the closure
// before the user commits rather than quietly widening what they asked for.

#include "core/drafts/draft_workspace.h"

#include <map>
#include <string>
#include <vector>

namespace core::drafts {

// group id -> the groups it cannot be promoted without, in sequence order.
//
// Inferred from identity: a group depends on another when it references, updates
// or removes an element that other group created, or wires a relationship to
// one. Anything a source declared explicitly in `depends_on_group_ids` is kept.
std::map<std::string, std::vector<std::string>> InferDraftDependencies(const DraftWorkspace& workspace);

// Writes the inferred dependencies onto the workspace's groups, so they are
// persisted and the UI can explain a closure rather than assert one.
void ApplyInferredDependencies(DraftWorkspace& workspace);

// `selection` plus everything it transitively depends on, in sequence order.
//
// Promotion always uses this, never the raw selection: the whole point is that
// asking for one visible change cannot silently leave out what makes it
// meaningful.
std::vector<std::string> DependencyClosure(const DraftWorkspace& workspace, const std::vector<std::string>& selection);

// The groups that would be left dangling by rejecting `selection`, transitively.
//
// Rejecting a group whose creations another group edits leaves that second group
// referring to an element that will never exist, so the user is offered a
// cascade rather than discovering it at promotion.
std::vector<std::string> DependentsOf(const DraftWorkspace& workspace, const std::vector<std::string>& selection);

} // namespace core::drafts
