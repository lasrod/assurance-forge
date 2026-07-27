#pragma once

// The change sets currently in play, owned by the application.
//
// One per connection at a time: an agent works on one change until it is
// accepted, rejected or abandoned. Two connected clients get two, and the
// reviewer can tell them apart, but a single agent cannot open several and lose
// track of which one it is adding to.
//
// Held in memory. A change set is a proposal in progress, not project data, and
// writing it into the project would put a second writer back in the directory
// this design just cleared.

#include "core/changesets/change_set.h"

#include <cstdint>
#include <string>
#include <vector>

namespace core::changesets {

class ChangeSetStore {
  public:
    // Starts a change set for `connection_id`, replacing whatever that
    // connection had open. Returns its id.
    std::string Begin(std::uint64_t connection_id, std::string title, std::string summary,
                      std::string intent, std::string client_label);

    // Null when no change set has that id, or it is no longer open.
    ChangeSet*       FindOpen(const std::string& id);
    const ChangeSet* Find(const std::string& id) const;

    // The change set `connection_id` is building, if any.
    ChangeSet* OpenFor(std::uint64_t connection_id);

    // Appends operations. Rejected as a whole if the result would not apply, so
    // a change set never holds a patch that cannot be previewed -- which is what
    // lets the canvas render one at any moment.
    bool Stage(const std::string& id, const std::vector<reviews::PatchOperation>& operations,
               const parser::AssuranceCase& committed, std::string& error);

    // Drops staged operations from the end, so an agent can respond to "not
    // there" without starting over. `count` larger than what is staged clears it.
    bool Unstage(const std::string& id, std::size_t count, std::string& error);

    bool MarkReady(const std::string& id, std::string& error);
    bool MarkApplied(const std::string& id);
    bool Discard(const std::string& id, std::string& error);

    // Everything still open, oldest first.
    std::vector<const ChangeSet*> Open() const;
    // Whether anything is open, so the canvas can skip diffing entirely.
    bool                          has_open() const;

    // Bumped by every call that changes what a reader would see. The
    // application compares it each frame to decide whether the canvas needs
    // rebuilding.
    //
    // Necessary because staging is deliberately *not* a model mutation: nothing
    // it does marks the derived views dirty, so without this the preview only
    // appeared when something unrelated happened to rebuild the tree -- which
    // presented as an agent's work being invisible until the user clicked away
    // to another argument file and back.
    std::uint64_t revision() const { return revision_; }

    void Clear();

  private:
    std::vector<ChangeSet> change_sets_;
    std::vector<std::uint64_t> owners_;
    std::uint64_t              next_serial_ = 1;
    std::uint64_t              revision_    = 0;
};

} // namespace core::changesets
