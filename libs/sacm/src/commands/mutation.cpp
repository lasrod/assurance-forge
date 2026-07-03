#include "sacm/commands/mutation.h"

namespace sacm::commands {

namespace {

std::vector<model::ElementId> ids_with_change(const std::vector<ChangeRecord>& changes,
                                              ChangeRecord::Change first,
                                              ChangeRecord::Change second) {
    std::vector<model::ElementId> ids;
    for (const ChangeRecord& record : changes) {
        if (record.change == first || record.change == second) {
            ids.push_back(record.id);
        }
    }
    return ids;
}

}  // namespace

std::vector<model::ElementId> MutationResult::created_ids() const {
    return ids_with_change(changes, ChangeRecord::Change::Created, ChangeRecord::Change::Created);
}

std::vector<model::ElementId> MutationResult::deleted_ids() const {
    return ids_with_change(changes, ChangeRecord::Change::Deleted,
                           ChangeRecord::Change::RelationshipDeleted);
}

}  // namespace sacm::commands
