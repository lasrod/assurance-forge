#pragma once

#include "core/reviews/review_proposal.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace core::reviews {

class ReviewProposalManager {
public:
    ReviewProposalManager() = default;
    explicit ReviewProposalManager(std::filesystem::path project_root);

    void SetProjectRoot(std::filesystem::path project_root);

    std::vector<ReviewProposalSummary> ListProposals(const parser::AssuranceCase* current_model = nullptr) const;
    std::optional<ReviewProposal> LoadProposal(const std::string& proposal_id, std::string& error) const;
    bool SaveProposal(const ReviewProposal& proposal, std::filesystem::path* relative_path, std::string& error) const;
    bool DeleteProposal(const std::string& proposal_id, std::string& error) const;

    std::filesystem::path ProposalsDirectory() const;
    std::filesystem::path ProposalPath(const std::string& proposal_id) const;

    // Force the next ListProposals call to re-scan and re-evaluate. Callers
    // should invoke this whenever they mutate the AssuranceCase model in
    // place, since the cache identifies the model only by pointer.
    void InvalidateProposalCache() const;

private:
    std::filesystem::path project_root_;

    // Cache for ListProposals. Disk scan + JSON parse is expensive and is
    // called every frame from the project explorer; we memoise based on a
    // cheap directory signature (file count + sum of mtimes) plus the
    // current_model pointer used to evaluate validity.
    mutable std::vector<ReviewProposalSummary> cached_summaries_;
    mutable std::uint64_t cached_dir_signature_ = 0;
    mutable const parser::AssuranceCase* cached_model_ptr_ = nullptr;
    mutable std::filesystem::path cached_project_root_;
    mutable bool cache_valid_ = false;
};

} // namespace core::reviews