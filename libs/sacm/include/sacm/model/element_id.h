#pragma once

#include <compare>
#include <functional>
#include <string>
#include <utility>

namespace sacm::model {

// Strongly typed SACM element identifier (XMI xmi:id / id attribute value).
// IDs are stable: caller-provided IDs are preserved, generated IDs never
// change after creation.
class ElementId {
  public:
    ElementId() = default;
    explicit ElementId(std::string value) : value_(std::move(value)) {}

    const std::string& value() const { return value_; }
    bool empty() const { return value_.empty(); }

    friend auto operator<=>(const ElementId&, const ElementId&) = default;

  private:
    std::string value_;
};

}  // namespace sacm::model

template <>
struct std::hash<sacm::model::ElementId> {
    std::size_t operator()(const sacm::model::ElementId& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};
