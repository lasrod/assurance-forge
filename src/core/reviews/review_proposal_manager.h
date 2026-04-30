#pragma once

#include "core/reviews/review_proposal.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace core {

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

private:
    std::filesystem::path project_root_;
};

}  // namespace core