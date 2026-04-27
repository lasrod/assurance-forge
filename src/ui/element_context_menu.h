#pragma once

#include "core/element_factory.h"
#include "parser/xml_parser.h"

#include <functional>
#include <string>

namespace ui {

struct ElementContextActions {
    std::function<void(core::NewElementKind)> add_child;
    std::function<void(core::RemoveMode)> remove_selected;
    std::function<void(const char*)> not_implemented;
};

void RenderAddElementMenu(const ElementContextActions& actions);

void RenderRemoveSubmenu(const parser::AssuranceCase* active_case,
                         const std::string& selected_id,
                         const ElementContextActions& actions);

}  // namespace ui
