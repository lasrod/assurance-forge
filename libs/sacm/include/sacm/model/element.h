#pragma once

#include "sacm/metadata/element_kind.h"
#include "sacm/model/element_id.h"
#include "sacm/model/lang_string.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sacm::detail {
// Internal mutation gateway. All model fields are private; commands and the
// XMI reader mutate exclusively through this struct (defined in
// libs/sacm/src/model/access.h, not installed). This mechanically enforces
// the rule that documents change only through Document::apply or load.
struct Access;
}  // namespace sacm::detail

namespace sacm::model {

using metadata::ElementKind;

// Abstract base of every identifiable SACM element (clause 8.2).
// Identity: `id` is the XMI serialization identity (xmi:id); `gid` is SACM's
// optional model-global identifier.
class SACMElement {
  public:
    virtual ~SACMElement() = default;

    SACMElement(const SACMElement&) = delete;
    SACMElement& operator=(const SACMElement&) = delete;

    ElementKind kind() const { return kind_; }
    const ElementId& id() const { return id_; }
    const std::string& gid() const { return gid_; }
    bool is_citation() const { return is_citation_; }
    bool is_abstract() const { return is_abstract_; }
    const std::optional<ElementId>& cited_element() const { return cited_element_; }
    const std::optional<ElementId>& abstract_form() const { return abstract_form_; }

    // Containment parent; nullptr for document roots.
    const SACMElement* parent() const { return parent_; }

  protected:
    SACMElement(ElementKind kind, ElementId id) : kind_(kind), id_(std::move(id)) {}

  private:
    friend struct sacm::detail::Access;

    ElementKind kind_;
    ElementId id_;
    std::string gid_;
    bool is_citation_ = false;
    bool is_abstract_ = false;
    std::optional<ElementId> cited_element_;
    std::optional<ElementId> abstract_form_;
    SACMElement* parent_ = nullptr;
};

// Abstract base of Description/ImplementationConstraint/Note/TaggedValue
// (clause 8.7): auxiliary content attached to a ModelElement.
class UtilityElement : public SACMElement {
  public:
    // content: MultiLangString[0..1]
    const MultiLangString& content() const { return content_; }

  protected:
    using SACMElement::SACMElement;

  private:
    friend struct sacm::detail::Access;

    MultiLangString content_;
};

class Description final : public UtilityElement {
  public:
    explicit Description(ElementId id) : UtilityElement(ElementKind::Description, std::move(id)) {}
};

class ImplementationConstraint final : public UtilityElement {
  public:
    explicit ImplementationConstraint(ElementId id)
        : UtilityElement(ElementKind::ImplementationConstraint, std::move(id)) {}
};

class Note final : public UtilityElement {
  public:
    explicit Note(ElementId id) : UtilityElement(ElementKind::Note, std::move(id)) {}
};

// Key/value extension mechanism (clause 8.12): key is the MultiLangString
// key, the inherited content is the value.
class TaggedValue final : public UtilityElement {
  public:
    explicit TaggedValue(ElementId id) : UtilityElement(ElementKind::TaggedValue, std::move(id)) {}

    const MultiLangString& key() const { return key_; }

  private:
    friend struct sacm::detail::Access;

    MultiLangString key_;
};

// Abstract base of the identifiable, nameable model elements (clause 8.6).
// name is LangString[1]; description is Description[0..1] per the
// specification text (the machine-readable model allows [0..*], so the
// model stores a vector and full validation flags surplus entries).
class ModelElement : public SACMElement {
  public:
    const LangString& name() const { return name_; }
    const std::vector<std::unique_ptr<Description>>& descriptions() const { return descriptions_; }
    const std::vector<std::unique_ptr<ImplementationConstraint>>& implementation_constraints() const {
        return implementation_constraints_;
    }
    const std::vector<std::unique_ptr<Note>>& notes() const { return notes_; }
    const std::vector<std::unique_ptr<TaggedValue>>& tagged_values() const { return tagged_values_; }

    // Primary description content, or empty when there is no description.
    const MultiLangString& description() const;

  protected:
    using SACMElement::SACMElement;

  private:
    friend struct sacm::detail::Access;

    LangString name_;
    std::vector<std::unique_ptr<Description>> descriptions_;
    std::vector<std::unique_ptr<ImplementationConstraint>> implementation_constraints_;
    std::vector<std::unique_ptr<Note>> notes_;
    std::vector<std::unique_ptr<TaggedValue>> tagged_values_;
};

// Abstract base for everything that can be referenced as an artifact
// (clause 8.10); base of all AssuranceCase/Terminology/Argumentation/
// Artifact package elements.
class ArtifactElement : public ModelElement {
  protected:
    using ModelElement::ModelElement;
};

}  // namespace sacm::model
