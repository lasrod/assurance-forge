#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sacm {

enum class SacmPackageNodeType {
    SacmFile,
    AssuranceCasePackage,
    ArgumentPackage,
    ArtifactPackage,
    TerminologyPackage,
    AssuranceCasePackageInterface,
    ArgumentPackageInterface,
    ArtifactPackageInterface,
    TerminologyPackageInterface,
    AssuranceCasePackageBinding,
    ArgumentPackageBinding,
    ArtifactPackageBinding,
    TerminologyPackageBinding,
    UnknownPackage,
};

struct SacmPackageTreeNode {
    std::string id;
    std::string gid;
    std::string displayName;
    std::string description;
    std::string xmlLocalName;
    SacmPackageNodeType type = SacmPackageNodeType::UnknownPackage;
    std::vector<SacmPackageTreeNode> children;
};

struct SacmPackageTreeResult {
    bool success = false;
    std::string error_message;
    std::filesystem::path source_path;
    SacmPackageTreeNode root;
};

const char* SacmPackageNodeTypeToString(SacmPackageNodeType type);
const char* SacmPackageNodeTypeToDisplayString(SacmPackageNodeType type);

SacmPackageTreeResult build_sacm_package_tree(const std::filesystem::path& file_path);
SacmPackageTreeResult build_sacm_package_tree_from_string(const std::string& xml_content,
                                                          const std::string& file_display_name = "SACM file");

} // namespace sacm
