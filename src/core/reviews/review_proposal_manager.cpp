#include "core/reviews/review_proposal_manager.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace core::reviews {

namespace {

std::string ReadTextFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        error = "Could not read " + path.string();
        return {};
    }
    return buffer.str();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create " + path.parent_path().string() + ": " + ec.message();
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Could not write " + path.string();
        return false;
    }
    file << content;
    if (!file.good()) {
        error = "Could not finish writing " + path.string();
        return false;
    }
    return true;
}

std::string ProposalIdFromPath(const std::filesystem::path& path) {
    std::string name = path.filename().generic_string();
    const std::string suffix = ".afpatch.json";
    if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.erase(name.size() - suffix.size());
    }
    return name;
}

}  // namespace

ReviewProposalManager::ReviewProposalManager(std::filesystem::path project_root)
    : project_root_(std::move(project_root)) {}

void ReviewProposalManager::SetProjectRoot(std::filesystem::path project_root) {
    project_root_ = std::move(project_root);
}

std::filesystem::path ReviewProposalManager::ProposalsDirectory() const {
    return project_root_ / "reviews" / "proposals";
}

std::filesystem::path ReviewProposalManager::ProposalPath(const std::string& proposal_id) const {
    return ProposalsDirectory() / (proposal_id + ".afpatch.json");
}

std::vector<ReviewProposalSummary> ReviewProposalManager::ListProposals(const parser::AssuranceCase* current_model) const {
    std::vector<ReviewProposalSummary> summaries;
    std::error_code ec;
    const std::filesystem::path directory = ProposalsDirectory();
    if (!std::filesystem::exists(directory, ec)) return summaries;

    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        std::string error;
        ReviewProposal proposal;
        if (!DeserializeReviewProposal(ReadTextFile(entry.path(), error), proposal, error)) {
            ReviewProposalSummary summary;
            summary.id = ProposalIdFromPath(entry.path());
            summary.title = summary.id;
            summary.relative_path = std::filesystem::relative(entry.path(), project_root_, ec);
            if (ec) summary.relative_path = entry.path().filename();
            summary.validity = {ProposalValidity::Broken, error};
            summaries.push_back(std::move(summary));
            continue;
        }

        ReviewProposalSummary summary;
        summary.id = proposal.id;
        summary.title = proposal.title;
        summary.summary = proposal.summary;
        summary.review_item_id = proposal.review_item_id;
        summary.anchor_element_id = proposal.anchor_element_id;
        summary.relative_path = std::filesystem::relative(entry.path(), project_root_, ec);
        if (ec) summary.relative_path = entry.path().filename();
        summary.validity = current_model ? EvaluateReviewProposalValidity(proposal, *current_model)
                                         : ProposalValidityResult{ProposalValidity::Valid, {}};
        summaries.push_back(std::move(summary));
    }

    std::sort(summaries.begin(), summaries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    return summaries;
}

std::optional<ReviewProposal> ReviewProposalManager::LoadProposal(const std::string& proposal_id, std::string& error) const {
    ReviewProposal proposal;
    if (!DeserializeReviewProposal(ReadTextFile(ProposalPath(proposal_id), error), proposal, error)) {
        return std::nullopt;
    }
    return proposal;
}

bool ReviewProposalManager::SaveProposal(const ReviewProposal& proposal,
                                         std::filesystem::path* relative_path,
                                         std::string& error) const {
    if (proposal.id.empty()) {
        error = "Proposal id is required.";
        return false;
    }
    const std::filesystem::path absolute_path = ProposalPath(proposal.id);
    if (!WriteTextFile(absolute_path, SerializeReviewProposal(proposal), error)) return false;
    if (relative_path) {
        std::error_code ec;
        *relative_path = std::filesystem::relative(absolute_path, project_root_, ec);
        if (ec) *relative_path = absolute_path.filename();
    }
    return true;
}

bool ReviewProposalManager::DeleteProposal(const std::string& proposal_id, std::string& error) const {
    std::error_code ec;
    const std::filesystem::path path = ProposalPath(proposal_id);
    if (!std::filesystem::exists(path, ec)) {
        error = "Proposal file does not exist: " + proposal_id;
        return false;
    }
    if (!std::filesystem::remove(path, ec) || ec) {
        error = "Could not delete proposal file: " + ec.message();
        return false;
    }
    error.clear();
    return true;
}

}  // namespace core::reviews