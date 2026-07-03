#include "sacm/compare/semantic_compare.h"

#include "model/traverse.h"

#include "sacm/model/assurance_case.h"
#include "sacm/model/document.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace sacm::compare {

namespace {

using model::SACMElement;

// Canonical string form of a MultiLangString: sorted (lang, content,
// expression) tuples. Order-insensitive by design.
std::string canonical_multi_lang(const model::MultiLangString& value) {
    std::vector<std::string> entries;
    entries.reserve(value.values.size());
    for (const model::LangString& entry : value.values) {
        entries.push_back(std::format(
            "{}={}{}", entry.lang, entry.content,
            entry.expression_ref.has_value() ? "@" + entry.expression_ref->value() : ""));
    }
    std::ranges::sort(entries);
    std::string joined;
    for (const std::string& entry : entries) {
        joined += entry;
        joined.push_back('|');
    }
    return joined;
}

// Comparable scalar state (attributes + name), with defaults normalized:
// only non-default values appear in the snapshot.
std::map<std::string, std::string> attribute_snapshot(const SACMElement& element) {
    std::map<std::string, std::string> snapshot;
    if (!element.gid().empty()) {
        snapshot["gid"] = element.gid();
    }
    if (element.is_citation()) {
        snapshot["isCitation"] = "true";
    }
    if (element.is_abstract()) {
        snapshot["isAbstract"] = "true";
    }

    if (const auto* model_element = dynamic_cast<const model::ModelElement*>(&element)) {
        const model::LangString& name = model_element->name();
        if (!name.content.empty() || !name.lang.empty()) {
            snapshot["name"] = std::format("{}={}", name.lang, name.content);
        }
    }
    if (const auto* utility = dynamic_cast<const model::UtilityElement*>(&element)) {
        if (!utility->content().empty()) {
            snapshot["content"] = canonical_multi_lang(utility->content());
        }
    }
    if (const auto* tagged = dynamic_cast<const model::TaggedValue*>(&element)) {
        if (!tagged->key().empty()) {
            snapshot["key"] = canonical_multi_lang(tagged->key());
        }
    }
    if (const auto* assertion = dynamic_cast<const model::Assertion*>(&element)) {
        if (assertion->assertion_declaration() != model::AssertionDeclaration::Asserted) {
            snapshot["assertionDeclaration"] =
                std::string(model::assertion_declaration_name(assertion->assertion_declaration()));
        }
    }
    if (const auto* rel = dynamic_cast<const model::AssertedRelationship*>(&element)) {
        if (rel->is_counter()) {
            snapshot["isCounter"] = "true";
        }
    }
    if (const auto* artifact = dynamic_cast<const model::Artifact*>(&element)) {
        if (!artifact->version().empty()) {
            snapshot["version"] = artifact->version();
        }
        if (!artifact->date().empty()) {
            snapshot["date"] = artifact->date();
        }
    }
    if (const auto* activity = dynamic_cast<const model::Activity*>(&element)) {
        if (!activity->start_time().empty()) {
            snapshot["startTime"] = activity->start_time();
        }
        if (!activity->end_time().empty()) {
            snapshot["endTime"] = activity->end_time();
        }
    }
    if (const auto* event = dynamic_cast<const model::Event*>(&element)) {
        if (!event->date().empty()) {
            snapshot["date"] = event->date();
        }
    }
    if (const auto* expr_element = dynamic_cast<const model::ExpressionElement*>(&element)) {
        if (!expr_element->value().empty()) {
            snapshot["value"] = expr_element->value();
        }
    }
    if (const auto* term = dynamic_cast<const model::Term*>(&element)) {
        if (!term->external_reference().empty()) {
            snapshot["externalReference"] = term->external_reference();
        }
    }
    if (!element.preserved_content().empty()) {
        std::vector<std::string> fragments = element.preserved_content();
        std::ranges::sort(fragments);
        std::string joined;
        for (const std::string& fragment : fragments) {
            joined += fragment;
            joined.push_back('\x1f');
        }
        snapshot["preservedContent"] = std::move(joined);
    }
    return snapshot;
}

// role -> sorted target ids (multiset semantics; ordering not semantic).
std::map<std::string, std::vector<std::string>> reference_snapshot(const SACMElement& element) {
    std::map<std::string, std::vector<std::string>> snapshot;
    model::traverse::for_each_reference(element, [&](const model::traverse::ReferenceUse& use) {
        snapshot[std::string(use.role)].push_back(use.target->value());
    });
    for (auto& [role, targets] : snapshot) {
        std::ranges::sort(targets);
    }
    return snapshot;
}

void compare_elements(const std::string& path, const SACMElement& left, const SACMElement& right,
                      std::vector<SemanticDifference>& differences) {
    if (left.kind() != right.kind()) {
        differences.push_back(SemanticDifference{
            .path = path,
            .category = "kind",
            .message = std::format("kind differs: {} vs {}", metadata::kind_name(left.kind()),
                                   metadata::kind_name(right.kind())),
        });
        return;
    }

    const auto left_attrs = attribute_snapshot(left);
    const auto right_attrs = attribute_snapshot(right);
    if (left_attrs != right_attrs) {
        for (const auto& [key, value] : left_attrs) {
            const auto it = right_attrs.find(key);
            if (it == right_attrs.end()) {
                differences.push_back(SemanticDifference{
                    path, "attribute", std::format("'{}' missing on right (left: '{}')", key, value)});
            } else if (it->second != value) {
                differences.push_back(SemanticDifference{
                    path, "attribute",
                    std::format("'{}' differs: '{}' vs '{}'", key, value, it->second)});
            }
        }
        for (const auto& [key, value] : right_attrs) {
            if (!left_attrs.contains(key)) {
                differences.push_back(SemanticDifference{
                    path, "attribute", std::format("'{}' missing on left (right: '{}')", key, value)});
            }
        }
    }

    const auto left_refs = reference_snapshot(left);
    const auto right_refs = reference_snapshot(right);
    if (left_refs != right_refs) {
        differences.push_back(SemanticDifference{
            path, "reference", "reference targets differ"});
    }

    // Children paired by id, order-insensitive.
    std::map<std::string, const SACMElement*> left_children;
    std::map<std::string, const SACMElement*> right_children;
    model::traverse::for_each_child(left, [&](const SACMElement& child) {
        left_children[child.id().value()] = &child;
    });
    model::traverse::for_each_child(right, [&](const SACMElement& child) {
        right_children[child.id().value()] = &child;
    });
    for (const auto& [id, child] : left_children) {
        const auto it = right_children.find(id);
        if (it == right_children.end()) {
            differences.push_back(SemanticDifference{
                path, "missing",
                std::format("{} '{}' missing on right", metadata::kind_name(child->kind()), id)});
            continue;
        }
        compare_elements(path.empty() ? id : path + "/" + id, *child, *it->second, differences);
    }
    for (const auto& [id, child] : right_children) {
        if (!left_children.contains(id)) {
            differences.push_back(SemanticDifference{
                path, "extra",
                std::format("{} '{}' missing on left", metadata::kind_name(child->kind()), id)});
        }
    }
}

}  // namespace

std::vector<SemanticDifference> semantic_compare(const model::Document& left,
                                                 const model::Document& right) {
    std::vector<SemanticDifference> differences;

    std::map<std::string, const SACMElement*> left_roots;
    std::map<std::string, const SACMElement*> right_roots;
    for (const auto& root : left.roots()) {
        left_roots[root->id().value()] = root.get();
    }
    for (const auto& root : left.other_roots()) {
        left_roots[root->id().value()] = root.get();
    }
    for (const auto& root : right.roots()) {
        right_roots[root->id().value()] = root.get();
    }
    for (const auto& root : right.other_roots()) {
        right_roots[root->id().value()] = root.get();
    }

    for (const auto& [id, root] : left_roots) {
        const auto it = right_roots.find(id);
        if (it == right_roots.end()) {
            differences.push_back(SemanticDifference{
                "", "missing",
                std::format("root {} '{}' missing on right", metadata::kind_name(root->kind()),
                            id)});
            continue;
        }
        compare_elements(id, *root, *it->second, differences);
    }
    for (const auto& [id, root] : right_roots) {
        if (!left_roots.contains(id)) {
            differences.push_back(SemanticDifference{
                "", "extra",
                std::format("root {} '{}' missing on left", metadata::kind_name(root->kind()),
                            id)});
        }
    }
    return differences;
}

}  // namespace sacm::compare
