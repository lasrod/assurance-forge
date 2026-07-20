// Mechanical drift check between the library's element kinds and the
// committed metamodel inventory generated from the normative OMG model
// (docs/sacm/sacm-2.3-metamodel-inventory.md). Skipped in standalone
// builds without the repository checkout.

#include "sacm/metadata/element_kind.h"

#include "io/name_tables.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

namespace {

using sacm::metadata::ElementKind;

constexpr ElementKind kAllKinds[] = {
    ElementKind::Description, ElementKind::ImplementationConstraint, ElementKind::Note,
    ElementKind::TaggedValue, ElementKind::AssuranceCasePackage,
    ElementKind::AssuranceCasePackageInterface, ElementKind::AssuranceCasePackageBinding,
    ElementKind::TerminologyPackage, ElementKind::TerminologyPackageInterface,
    ElementKind::TerminologyPackageBinding, ElementKind::TerminologyGroup, ElementKind::Category,
    ElementKind::Expression, ElementKind::Term, ElementKind::ArgumentPackage,
    ElementKind::ArgumentPackageInterface, ElementKind::ArgumentPackageBinding,
    ElementKind::ArgumentGroup, ElementKind::Claim, ElementKind::ArgumentReasoning,
    ElementKind::ArtifactReference, ElementKind::AssertedInference, ElementKind::AssertedEvidence,
    ElementKind::AssertedContext, ElementKind::AssertedArtifactSupport,
    ElementKind::AssertedArtifactContext, ElementKind::ArtifactPackage,
    ElementKind::ArtifactPackageInterface, ElementKind::ArtifactPackageBinding,
    ElementKind::ArtifactGroup, ElementKind::Artifact, ElementKind::ArtifactAssetRelationship,
    ElementKind::Activity, ElementKind::Event, ElementKind::Participant, ElementKind::Technique,
    ElementKind::Resource, ElementKind::Property,
};

// Value types in the inventory's concrete-class table that are not
// SACMElements (no ElementKind).
const std::set<std::string> kValueTypes = {"LangString", "MultiLangString", "ExpressionLangString"};

TEST(Sacm23Metamodel, SACM23_LIB_001_ElementKindsMatchNormativeInventory) {
#ifndef SACM_REPO_FIXTURES_DIR
    GTEST_SKIP() << "repository inventory unavailable in standalone builds";
#else
    const std::filesystem::path inventory_path = std::filesystem::path(SACM_REPO_FIXTURES_DIR) /
                                                 "docs" / "sacm" /
                                                 "sacm-2.3-metamodel-inventory.md";
    if (!std::filesystem::exists(inventory_path)) {
        GTEST_SKIP() << "inventory not found: " << inventory_path.string();
    }
    std::ifstream stream(inventory_path);
    std::string line;
    std::set<std::string> inventory_classes;
    bool in_table = false;
    // Parse the "Concrete class -> containment roles" table rows.
    const std::regex row(R"(^\| ([A-Za-z]+) \| [A-Za-z]+ \|)");
    while (std::getline(stream, line)) {
        if (line.rfind("## Concrete class", 0) == 0) {
            in_table = true;
            continue;
        }
        if (!in_table) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(line, match, row) && match[1] != "Concrete") {
            inventory_classes.insert(match[1]);
        }
    }
    ASSERT_FALSE(inventory_classes.empty()) << "no concrete classes parsed from the inventory";

    std::set<std::string> library_kinds;
    for (const ElementKind kind : kAllKinds) {
        library_kinds.insert(std::string(sacm::metadata::kind_name(kind)));
    }

    for (const std::string& class_name : inventory_classes) {
        if (kValueTypes.contains(class_name)) {
            continue;
        }
        EXPECT_TRUE(library_kinds.contains(class_name))
            << "inventory class not covered by the library: " << class_name;
    }
    for (const std::string& kind_name : library_kinds) {
        EXPECT_TRUE(inventory_classes.contains(kind_name))
            << "library kind not present in the normative inventory: " << kind_name;
    }
#endif
}

// The losslessness gate rejects unprefixed attributes outside a fixed table, so
// that table must not fall behind the normative model: a name added to the
// inventory but not the table would make valid SACM look like vendor content.
// The reverse direction is deliberately not checked -- the table also holds
// XMI infrastructure names and tolerant-mode shorthands that the inventory,
// which describes the model rather than the serialization, never mentions.
TEST(Sacm23Metamodel, SACM23_XMI_003_KnownAttributesCoverTheNormativeInventory) {
#ifndef SACM_REPO_FIXTURES_DIR
    GTEST_SKIP() << "repository inventory unavailable in standalone builds";
#else
    const std::filesystem::path inventory_path = std::filesystem::path(SACM_REPO_FIXTURES_DIR) /
                                                 "docs" / "sacm" /
                                                 "sacm-2.3-metamodel-inventory.md";
    if (!std::filesystem::exists(inventory_path)) {
        GTEST_SKIP() << "inventory not found: " << inventory_path.string();
    }
    std::ifstream stream(inventory_path);
    std::string line;
    std::set<std::string> inventory_names;
    // Attribute and reference-end tables both start with a lowerCamelCase name;
    // containment roles are child elements, not attributes, so they are skipped.
    enum class Section { None, Attribute, Reference } section = Section::None;
    const std::regex row(R"(^\| ([a-z][A-Za-z]*) \|)");
    while (std::getline(stream, line)) {
        if (line.starts_with("| Attribute")) {
            section = Section::Attribute;
            continue;
        }
        if (line.starts_with("| Reference end")) {
            section = Section::Reference;
            continue;
        }
        if (line.starts_with("| Containment role") || line.starts_with("#")) {
            section = Section::None;
            continue;
        }
        std::smatch match;
        if (section != Section::None && std::regex_search(line, match, row)) {
            inventory_names.insert(match[1]);
        }
    }
    ASSERT_FALSE(inventory_names.empty()) << "no attribute names parsed from the inventory";

    for (const std::string& name : inventory_names) {
        EXPECT_TRUE(sacm::io::detail::is_known_sacm_attribute(name))
            << "normative attribute/association end missing from the losslessness table: " << name
            << " -- valid SACM would be reported as unknown content";
    }
#endif
}

}  // namespace
