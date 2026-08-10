#include "ui/panels/element_panel.h"

#include "core/element_factory.h"
#include "core/terminology_scope_service.h"
#include "ui/fonts.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/i18n/localization.h"
#include "ui/panels/confidence_panel.h"
#include "ui/text_edit_session.h"
#include "ui/theme.h"
#include "ui/ui_state.h"
#include "ui/widgets/empty_state.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace ui::panels {

namespace {

// Find a parser element by ID (mutable).
parser::SacmElement* find_parser_element(parser::AssuranceCase* ac, const std::string& id) {
    if (!ac)
        return nullptr;
    for (auto& elem : ac->elements) {
        if (elem.id == id)
            return &elem;
    }
    return nullptr;
}

// Sync a parser element's editable fields into matching sacm model elements.
// We search across all argument packages, artifact packages, and terminology packages.
void sync_to_sacm(sacm::AssuranceCasePackage* pkg,
                  const std::string& id,
                  const std::string& new_name,
                  const std::string& new_description,
                  const std::string& new_content,
                  bool undeveloped,
                  const std::map<std::string, std::string>& name_langs,
                  const std::map<std::string, std::string>& desc_langs,
                  const std::map<std::string, std::string>& content_langs) {
    if (!pkg)
        return;

    // Helper lambda to update name_ml and description_ml from lang maps
    auto update_ml = [&](sacm::SacmElement& se) {
        se.name = new_name;
        se.name_ml.texts = name_langs;
        se.description = new_description;
        se.description_ml.texts = desc_langs;
    };

    for (auto& ap : pkg->argumentPackages) {
        for (auto& c : ap.claims) {
            if (c.id == id) {
                update_ml(c);
                c.content = new_content;
                c.content_ml.texts = content_langs;
                c.undeveloped = undeveloped;
                return;
            }
        }
        for (auto& ar : ap.argumentReasonings) {
            if (ar.id == id) {
                update_ml(ar);
                ar.content = new_content;
                ar.content_ml.texts = content_langs;
                ar.undeveloped = undeveloped;
                return;
            }
        }
        for (auto& ar : ap.artifactReferences) {
            if (ar.id == id) {
                update_ml(ar);
                return;
            }
        }
        for (auto& ai : ap.assertedInferences) {
            if (ai.id == id) {
                update_ml(ai);
                return;
            }
        }
        for (auto& ac : ap.assertedContexts) {
            if (ac.id == id) {
                update_ml(ac);
                return;
            }
        }
        for (auto& ae : ap.assertedEvidences) {
            if (ae.id == id) {
                update_ml(ae);
                return;
            }
        }
    }
    for (auto& artpkg : pkg->artifactPackages) {
        for (auto& a : artpkg.artifacts) {
            if (a.id == id) {
                update_ml(a);
                return;
            }
        }
    }
    for (auto& tp : pkg->terminologyPackages) {
        for (auto& e : tp.expressions) {
            if (e.id == id) {
                update_ml(e);
                return;
            }
        }
    }
}

// Helper: multi-line InputText with a buffer sized to accommodate edits.
// Returns true if the text was modified.
static bool
EditableTextField(const char* label, std::string& text, float width = -1.0f, ImGuiID* out_widget_id = nullptr) {
    ImGui::PushID(label);
    if (width > 0.0f)
        ImGui::SetNextItemWidth(width);
    else
        ImGui::SetNextItemWidth(-1);

    const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_NoHorizontalScroll | ImGuiInputTextFlags_WordWrap;
    bool changed = ImGui::InputTextMultiline("##edit", &text, ImVec2(-1, ImGui::GetTextLineHeight() * 5), flags);
    if (out_widget_id)
        *out_widget_id = ImGui::GetID("##edit");
    ImGui::PopID();
    return changed;
}

static bool EditableSingleLine(const char* label, std::string& text, ImGuiID* out_widget_id = nullptr) {
    char buf[512];
    size_t len = text.size();
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, text.c_str(), len);
    buf[len] = '\0';

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputText("##edit", buf, sizeof(buf));
    if (out_widget_id)
        *out_widget_id = ImGui::GetID("##edit");
    if (changed) {
        text = buf;
    }
    ImGui::PopID();
    return changed;
}

// Check if element has a translation entry for the given language (key exists)
static bool element_has_secondary(const parser::SacmElement& elem, const std::string& lang) {
    if (elem.name_langs.count(lang))
        return true;
    if (elem.description_langs.count(lang))
        return true;
    if (elem.content_langs.count(lang))
        return true;
    return false;
}

void InspectorFieldLabel(std::string_view label) {
    const Theme& theme = GetTheme();
    fonts::Scoped caption(fonts::Role::Caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
}

void InspectorSection(const char* icon, std::string_view title) {
    const Theme& theme = GetTheme();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.accent), "%s", icon);
    ImGui::SameLine(0.0f, 7.0f);
    {
        fonts::Scoped strong(fonts::Role::BodyStrong);
        ImGui::TextUnformatted(title.data(), title.data() + title.size());
    }
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.border, 0.72f)));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

static void RenderMetadataRow(const char* label, const std::string& value) {
    const Theme& theme = GetTheme();
    const char* display_value = value.empty() ? "-" : value.c_str();

    {
        fonts::Scoped caption(fonts::Role::Caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0.0f, 7.0f);
    {
        fonts::Scoped strong(fonts::Role::BodyStrong);
        ImGui::TextWrapped("%s", display_value);
    }
}

// What the working draft does to this element, and the decision about it.
//
// This is where "accept or decline *this* change" lives. Accept-all in the
// banner is the blunt instrument; a reviewer works claim by claim, and being
// able to see the accepted wording beside the proposed one -- with the source
// and the reasoning that produced it -- is the whole of what makes accepting it
// a judgement rather than a leap.
static void RenderDraftChangeSection(const std::string& element_id, const ElementDraftCallbacks* callbacks) {
    const UiState& state = GetUiState();
    const DraftElementDetailView& detail = state.draft_selected_detail;
    const bool draft_active = !state.draft_element_status.empty() || !state.draft_edge_status.empty();
    if (!draft_active)
        return;

    const Theme& theme = GetTheme();

    if (!detail.present || detail.element_id != element_id) {
        // Said rather than left blank. A draft is running and this element is
        // not part of it -- which is a different thing from the panel having
        // nothing to tell you, and the reader cannot distinguish the two from an
        // absent section.
        InspectorSection(ICON_FA_CODE_BRANCH, AF_TR("Working draft"));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
        ImGui::TextWrapped("%s",
                           AF_TR("The working draft does not change this element. Select an element marked "
                                 "NEW, EDIT or MULTIPLE CHANGES to review and accept it.")
                               .c_str());
        ImGui::PopStyleColor();
        return;
    }

    InspectorSection(ICON_FA_CODE_BRANCH, AF_TR("Working draft"));

    const char* change_label = "";
    switch (detail.change) {
    case core::drafts::DraftElementChange::Added:
        change_label = "This element is proposed and is not in the accepted argument.";
        break;
    case core::drafts::DraftElementChange::Modified:
        change_label = "This element is proposed to change.";
        break;
    case core::drafts::DraftElementChange::Removed:
        change_label = "This element is proposed for removal.";
        break;
    case core::drafts::DraftElementChange::Unchanged:
        return;
    }
    ImGui::TextWrapped("%s", ui::i18n::tr(change_label).c_str());

    // Side by side and per field, because "what changed" is the question a
    // reviewer is actually asking, and switching to the accepted-baseline view
    // to answer it loses the place they were reading.
    for (const DraftFieldChangeView& change : detail.field_changes) {
        InspectorFieldLabel(ui::i18n::trf("{0} — accepted", change.field_label));
        ImGui::TextWrapped("%s", change.accepted.empty() ? AF_TR("(empty)").c_str() : change.accepted.c_str());
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        InspectorFieldLabel(ui::i18n::trf("{0} — working draft", change.field_label));
        ImGui::TextWrapped("%s", change.working.empty() ? AF_TR("(empty)").c_str() : change.working.c_str());
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    InspectorFieldLabel(AF_TR("Contributions"));
    for (const DraftContributionView& contribution : detail.contributions) {
        ImGui::BulletText("%s",
                          ui::i18n::trf("{0} — {1}",
                                        contribution.source_label,
                                        contribution.title.empty() ? contribution.group_id : contribution.title)
                              .c_str());
        if (!contribution.rationale.empty()) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
            ImGui::TextWrapped("%s", contribution.rationale.c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    if (!detail.also_accepts_titles.empty()) {
        // Never widen the selection silently. Accepting a reworded claim that
        // another group created has to take that group too, and the user is told
        // so before they press the button, not after.
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        std::string also;
        for (const std::string& title : detail.also_accepts_titles) {
            if (!also.empty())
                also += ", ";
            also += title;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.attention));
        ImGui::TextWrapped("%s", ui::i18n::trf("Accepting this also accepts: {0}", also).c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    const bool blocked = !detail.blocked_reason.empty();
    ImGui::BeginDisabled(blocked || callbacks == nullptr || !callbacks->accept_groups);
    if (ImGui::Button(AF_TR("Accept this change").c_str()) && callbacks && callbacks->accept_groups)
        callbacks->accept_groups(detail.closure_group_ids);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(callbacks == nullptr || !callbacks->reject_groups);
    if (ImGui::Button(AF_TR("Reject this change").c_str()) && callbacks && callbacks->reject_groups)
        callbacks->reject_groups(detail.contributing_group_ids);
    ImGui::EndDisabled();

    if (blocked) {
        // Disabled with the reason beside it, rather than a button that appears
        // to work and does nothing.
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.attention));
        ImGui::TextWrapped("%s", detail.blocked_reason.c_str());
        ImGui::PopStyleColor();
    }
}

static void RenderElementMetadata(const parser::SacmElement& elem) {
    const Theme& theme = GetTheme();
    // Two text rows, in a two-column table, inside a bordered child.
    //
    // The original sum left out the table's cell padding and the child border,
    // which is why the id and the type were clipped. Both are added here.
    //
    // **Not `ImGuiChildFlags_AutoResizeY`**, which is the obvious fix and is
    // wrong inside a scrolling panel: an auto-resizing child is not measured
    // while it is clipped, so scrolling this card out of view collapses its
    // height, shrinks the panel's content extent, and snaps the scroll straight
    // back to the top. That reads as "the scrollbar does not work", and it is
    // the reason this height is computed rather than measured.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float card_height = fonts::SizeFor(fonts::Role::Caption) + fonts::SizeFor(fonts::Role::BodyStrong) +
                              style.ItemSpacing.y + style.CellPadding.y * 2.0f + style.WindowPadding.y * 2.0f +
                              style.ChildBorderSize * 2.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.surface_2, 0.76f)));
    ImGui::BeginChild(
        "##element_metadata", ImVec2(0.0f, card_height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTable("##element_metadata_columns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        InspectorFieldLabel(AF_TR("ID"));
        {
            fonts::Scoped strong(fonts::Role::BodyStrong);
            ImGui::TextUnformatted(elem.id.c_str());
        }
        ImGui::TableNextColumn();
        InspectorFieldLabel(AF_TR("Type"));
        {
            fonts::Scoped strong(fonts::Role::BodyStrong);
            ImGui::TextUnformatted(ElementTypeDisplayName(elem.type).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// Supported secondary languages (no special font requirements except ja which uses merged font)
static const char* const kLangCodes[] = {
    "ja", "de", "fr", "es", "it", "pt", "nl", "sv", "no", "da", "fi", "pl", "cs", "ro", "hu"};
static const char* const kLangLabels[] = {"Japanese",
                                          "German",
                                          "French",
                                          "Spanish",
                                          "Italian",
                                          "Portuguese",
                                          "Dutch",
                                          "Swedish",
                                          "Norwegian",
                                          "Danish",
                                          "Finnish",
                                          "Polish",
                                          "Czech",
                                          "Romanian",
                                          "Hungarian"};
static const int kLangCount = 15;

struct TerminologySuggestion {
    enum class Kind { UndefinedAcronym, AmbiguousTerm };

    Kind kind = Kind::UndefinedAcronym;
    std::string text;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
    std::vector<core::TerminologyScopedTermRef> candidates;
};

std::vector<TerminologySuggestion> BuildTerminologySuggestions(const sacm::AssuranceCasePackage* sacm_pkg,
                                                               const std::string& element_id,
                                                               const std::string& text,
                                                               const ElementTerminologyAssistCallbacks* callbacks) {
    std::vector<TerminologySuggestion> suggestions;
    if (!sacm_pkg || !callbacks || text.empty())
        return suggestions;

    core::TerminologyService terminology_service(*sacm_pkg);
    const std::vector<core::TermOccurrence> occurrences = terminology_service.DetectTermsInText(element_id, text);
    std::unordered_set<std::string> seen_terms;
    for (const core::TermOccurrence& occurrence : occurrences) {
        if (!seen_terms.insert(occurrence.text).second)
            continue;
        if (occurrence.kind == core::TermOccurrenceKind::UndefinedAcronym) {
            if (occurrence.resolution.status != core::TermResolutionStatus::None ||
                !occurrence.resolution.important_undefined) {
                continue;
            }
            if (callbacks->is_ignored && callbacks->is_ignored(element_id, occurrence.text))
                continue;
            suggestions.push_back({TerminologySuggestion::Kind::UndefinedAcronym,
                                   occurrence.text,
                                   occurrence.start_offset,
                                   occurrence.end_offset,
                                   {}});
        } else if (occurrence.resolution.status == core::TermResolutionStatus::Ambiguous) {
            suggestions.push_back({TerminologySuggestion::Kind::AmbiguousTerm,
                                   occurrence.text,
                                   occurrence.start_offset,
                                   occurrence.end_offset,
                                   occurrence.resolution.candidates});
        }
    }
    std::sort(suggestions.begin(),
              suggestions.end(),
              [](const TerminologySuggestion& left, const TerminologySuggestion& right) {
                  if (left.start_offset != right.start_offset)
                      return left.start_offset < right.start_offset;
                  return left.text < right.text;
              });
    return suggestions;
}

void RenderTerminologySuggestions(const sacm::AssuranceCasePackage* sacm_pkg,
                                  const std::string& element_id,
                                  const std::string& text,
                                  const ElementTerminologyAssistCallbacks* callbacks) {
    std::vector<TerminologySuggestion> suggestions = BuildTerminologySuggestions(sacm_pkg, element_id, text, callbacks);
    if (suggestions.empty())
        return;

    ImGui::Spacing();
    ImGui::TextUnformatted(AF_TR("Terminology suggestions").c_str());
    ImGui::Separator();
    for (const TerminologySuggestion& suggestion : suggestions) {
        ImGui::PushID(suggestion.text.c_str());
        if (suggestion.kind == TerminologySuggestion::Kind::AmbiguousTerm) {
            ImGui::TextColored(
                ui::GetWarningColor(), "%s", ui::i18n::trf("{0} has multiple meanings.", suggestion.text).c_str());
            int candidate_index = 0;
            for (const auto& candidate : suggestion.candidates) {
                ImGui::PushID(candidate_index++);
                const std::string term_name =
                    candidate.term && !candidate.term->name.empty() ? candidate.term->name : AF_TR("Unnamed term");
                ImGui::BulletText("%s", term_name.c_str());
                if (callbacks && callbacks->use_term_for_element) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(AF_TR("Use for this element").c_str())) {
                        callbacks->use_term_for_element(element_id, candidate.package_ref, candidate.term_ref);
                    }
                }
                ImGui::PopID();
            }
            if (callbacks && callbacks->define_term) {
                if (ImGui::Button(AF_TR("Create new meaning").c_str()))
                    callbacks->define_term(element_id, suggestion.text);
            }
            ImGui::PopID();
            continue;
        }

        ImGui::TextColored(ui::GetWarningColor(), "%s", ui::i18n::trf("{0} is not defined.", suggestion.text).c_str());
        if (callbacks && callbacks->define_term) {
            if (ImGui::Button(AF_TR("Define").c_str()))
                callbacks->define_term(element_id, suggestion.text);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(AF_TR("Define").c_str());
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (callbacks && callbacks->link_existing_term) {
            if (ImGui::Button(AF_TR("Link existing").c_str()))
                callbacks->link_existing_term(element_id, suggestion.text);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(AF_TR("Link existing").c_str());
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (callbacks && callbacks->ignore_term) {
            if (ImGui::Button(AF_TR("Ignore").c_str()))
                callbacks->ignore_term(element_id, suggestion.text);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(AF_TR("Ignore").c_str());
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
}

bool RenderReviewAttentionNotice(const ui::UiState& state,
                                 const std::string& element_id,
                                 const ElementTerminologyAssistCallbacks* terminology_callbacks) {
    auto summary_it = state.element_badge_summaries.find(element_id);
    if (summary_it == state.element_badge_summaries.end() || !summary_it->second.has_review_problem)
        return false;
    const core::ElementBadgeSummary& summary = summary_it->second;

    const std::string label = AF_TR("Unresolved review");
    const ui::Theme& theme = ui::GetTheme();
    const ImVec4 button_color = ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(theme.attention, 0.86f));
    const ImVec4 button_hover = ImGui::ColorConvertU32ToFloat4(theme.warning);
    const ImVec4 button_active = ImGui::ColorConvertU32ToFloat4(ui::ShadeColor(theme.attention, -0.16f));
    ImGui::PushStyleColor(ImGuiCol_Button, button_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active);
    if (ImGui::SmallButton(label.c_str()) && terminology_callbacks && terminology_callbacks->focus_review_tab) {
        terminology_callbacks->focus_review_tab();
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) {
        // Describe the review-derived problem specifically, since clicking
        // opens the Review tab. The overall `top_problem_message` may belong
        // to a higher-severity non-review problem (e.g. validation error) and
        // would be misleading here.
        const std::string& review_msg = summary.top_review_problem_message;
        if (review_msg.empty()) {
            ImGui::SetTooltip("%s", AF_TR("Open review comments or AI review failures for this element.").c_str());
        } else {
            ImGui::SetTooltip("%s", ui::i18n::trf("{0}\nClick to open the Review tab.", review_msg).c_str());
        }
    }
    return true;
}

// Renders the secondary-language translation-review warning + accept button when
// the element is flagged. Returns true if anything was drawn.
bool RenderTranslationReviewNotice(const std::string& element_id,
                                   const ElementTranslationReviewCallbacks* callbacks,
                                   bool read_only) {
    if (!callbacks || !callbacks->is_pending || !callbacks->is_pending(element_id))
        return false;

    ImGui::TextColored(
        ui::GetWarningColor(), "%s", AF_TR("Text changed — update both languages, then mark reviewed.").c_str());
    ImGui::BeginDisabled(read_only || !callbacks->accept);
    if (ImGui::SmallButton(AF_TR("Mark reviewed").c_str()) && callbacks->accept)
        callbacks->accept(element_id);
    ImGui::EndDisabled();
    return true;
}

} // namespace

// Literal AF_TR per case: the extractor only sees literals, so translating a
// variable would leave every one of these permanently English.
std::string ElementTypeDisplayName(const std::string& raw_type) {
    if (raw_type == "claim")
        return AF_TR("Claim");
    if (raw_type == "argumentreasoning")
        return AF_TR("Argument Reasoning");
    if (raw_type == "artifact")
        return AF_TR("Artifact");
    if (raw_type == "artifactreference")
        return AF_TR("Artifact Reference");
    if (raw_type == "expression")
        return AF_TR("Expression");
    if (raw_type == "assertedinference")
        return AF_TR("Asserted Inference");
    if (raw_type == "assertedcontext")
        return AF_TR("Asserted Context");
    if (raw_type == "assertedevidence")
        return AF_TR("Asserted Evidence");
    return raw_type;
}

bool ShowElementPanel(parser::AssuranceCase* ac,
                      sacm::AssuranceCasePackage* sacm_pkg,
                      const ElementTerminologyAssistCallbacks* terminology_callbacks,
                      const ElementConfidenceAssistCallbacks* confidence_callbacks,
                      const ElementTextEditCallbacks* text_edit_callbacks,
                      const ElementHistoryCallbacks* history_callbacks,
                      const ElementTranslationReviewCallbacks* translation_review_callbacks,
                      const ElementDraftCallbacks* draft_callbacks,
                      bool read_only) {
    const UiState& state = GetUiState();
    bool modified = false;

    if (state.selected_element_id.empty()) {
        ui::widgets::EmptyState(AF_TR("No element selected."));
        return false;
    }

    if (!ac) {
        ui::widgets::EmptyState(AF_TR("No safety case loaded."));
        return false;
    }

    // Find the element by ID
    parser::SacmElement* elem = find_parser_element(ac, state.selected_element_id);
    if (!elem) {
        ImGui::TextDisabled("%s", ui::i18n::trf("Element not found: {0}", state.selected_element_id).c_str());
        return false;
    }

    // Scope all widgets by element ID so switching elements creates fresh widget state
    ImGui::PushID(elem->id.c_str());

    const std::string sec_lang = state.active_secondary_lang;

    RenderElementMetadata(*elem);
    RenderDraftChangeSection(elem->id, draft_callbacks);

    if (RenderReviewAttentionNotice(state, elem->id, terminology_callbacks)) {
        ImGui::Spacing();
    }

    if (RenderTranslationReviewNotice(elem->id, translation_review_callbacks, read_only)) {
        ImGui::Spacing();
    }

    if (read_only) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::GetTheme().text_secondary));
        ImGui::TextWrapped("%s", AF_TR("Historical preview — fields are read-only. Return to latest to edit.").c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::BeginDisabled(true);
    }

    auto commit_if_finished =
        [&](ImGuiID id, const std::string& current_value, const char* field_token, const std::string& language) {
            ui::PendingTextEdit commit;
            if (TextEditSession::Track(id, current_value, elem->id, field_token, language, commit) &&
                text_edit_callbacks && text_edit_callbacks->commit_text_edit) {
                text_edit_callbacks->commit_text_edit(
                    commit.element_id, commit.field_token, commit.language, commit.original_value, commit.new_value);
            }
        };

    InspectorSection(ICON_FA_EDIT, AF_TR("Element"));

    // GSN notation identifier (editable independently of the read-only SACM id).
    InspectorFieldLabel(AF_TR("GSN identifier"));
    {
        std::string displayed_identifier = core::GsnIdentifierFor(*elem);
        ImGuiID widget_id = 0;
        if (EditableSingleLine("gsn_identifier", displayed_identifier, &widget_id)) {
            elem->gsn_identifier = displayed_identifier;
            modified = true;
        }
        commit_if_finished(widget_id, displayed_identifier, "gsn_identifier", "none");
    }
    ImGui::Spacing();

    // Name (editable)
    InspectorFieldLabel(AF_TR("Name"));
    {
        ImGuiID widget_id = 0;
        if (EditableSingleLine("name", elem->name, &widget_id)) {
            elem->name_langs["en"] = elem->name;
            modified = true;
        }
        commit_if_finished(widget_id, elem->name, "name", "en");
    }
    // Secondary language name (only show if this field has the secondary language)
    if (elem->name_langs.count(sec_lang)) {
        InspectorFieldLabel(ui::i18n::trf("Name ({0})", sec_lang));
        std::string sec_name = elem->name_langs.at(sec_lang);
        ImGuiID widget_id = 0;
        if (EditableSingleLine("name_sec", sec_name, &widget_id)) {
            elem->name_langs[sec_lang] = sec_name;
            modified = true;
        }
        commit_if_finished(widget_id, sec_name, "name", sec_lang);
    }
    ImGui::Spacing();

    // Content (editable, for claims and argument reasonings)
    bool has_content = (elem->type == "claim" || elem->type == "argumentreasoning");
    // The undeveloped decorator means "requires support that has not yet been
    // provided". GSN reaches an Assumption or a Justification by `InContextOf`,
    // never by `SupportedBy`, so there is no support for them to be missing --
    // and SACM records the decorator in the same single `assertionDeclaration`
    // that already says `assumed` or `axiomatic`, so the two cannot both be
    // stored. The control used to be offered on every claim, and the edit then
    // reported success and did nothing. Offer it only where it means something.
    const bool declaration_is_free = elem->assertion_declaration.empty() || elem->assertion_declaration == "asserted" ||
                                     elem->assertion_declaration == "needsSupport";
    bool supports_undeveloped = has_content && declaration_is_free;
    if (has_content) {
        InspectorFieldLabel(AF_TR("Content"));
        {
            ImGuiID widget_id = 0;
            if (EditableTextField("content", elem->content, -1.0f, &widget_id)) {
                elem->content_langs["en"] = elem->content;
                modified = true;
            }
            commit_if_finished(widget_id, elem->content, "content", "en");
        }
        RenderTerminologySuggestions(sacm_pkg, elem->id, elem->content, terminology_callbacks);
        // Secondary language content (only show if this field has the secondary language)
        if (elem->content_langs.count(sec_lang)) {
            InspectorFieldLabel(ui::i18n::trf("Content ({0})", sec_lang));
            std::string sec_content = elem->content_langs.at(sec_lang);
            ImGuiID widget_id = 0;
            if (EditableTextField("content_sec", sec_content, -1.0f, &widget_id)) {
                elem->content_langs[sec_lang] = sec_content;
                modified = true;
            }
            commit_if_finished(widget_id, sec_content, "content", sec_lang);
        }
        ImGui::Spacing();
    }

    if (supports_undeveloped) {
        bool undeveloped = elem->undeveloped;
        if (ImGui::Checkbox(AF_TR("Undeveloped").c_str(), &undeveloped)) {
            elem->undeveloped = undeveloped;
            modified = true;
        }
        ImGui::Spacing();
    }

    // Description (editable)
    InspectorFieldLabel(AF_TR("Description"));
    {
        ImGuiID widget_id = 0;
        if (EditableTextField("description", elem->description, -1.0f, &widget_id)) {
            elem->description_langs["en"] = elem->description;
            modified = true;
        }
        commit_if_finished(widget_id, elem->description, "description", "en");
    }
    if (!has_content)
        RenderTerminologySuggestions(sacm_pkg, elem->id, elem->description, terminology_callbacks);
    // Secondary language description (only show if this field has the secondary language)
    if (elem->description_langs.count(sec_lang)) {
        InspectorFieldLabel(ui::i18n::trf("Description ({0})", sec_lang));
        std::string sec_desc = elem->description_langs.at(sec_lang);
        ImGuiID widget_id = 0;
        if (EditableTextField("description_sec", sec_desc, -1.0f, &widget_id)) {
            elem->description_langs[sec_lang] = sec_desc;
            modified = true;
        }
        commit_if_finished(widget_id, sec_desc, "description", sec_lang);
    }

    // Translation controls: checkbox + language dropdown
    {
        UiState& mut_state = GetUiState();

        // Language selector dropdown
        InspectorSection(ICON_FA_LANGUAGE, AF_TR("Translation Language"));
        int current_lang_idx = 0;
        for (int i = 0; i < kLangCount; ++i) {
            if (mut_state.active_secondary_lang == kLangCodes[i]) {
                current_lang_idx = i;
                break;
            }
        }
        std::array<std::string, kLangCount> lang_label_storage;
        std::array<const char*, kLangCount> lang_label_ptrs;
        for (int i = 0; i < kLangCount; ++i) {
            lang_label_storage[i] = AF_TR(kLangLabels[i]);
            lang_label_ptrs[i] = lang_label_storage[i].c_str();
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##trans_lang", &current_lang_idx, lang_label_ptrs.data(), kLangCount)) {
            mut_state.active_secondary_lang = kLangCodes[current_lang_idx];
        }

        ImGui::Spacing();

        // Checkbox to enable/disable translation for this element
        bool has_trans = element_has_secondary(*elem, mut_state.active_secondary_lang);
        if (ImGui::Checkbox(AF_TR("Add Translation").c_str(), &has_trans)) {
            if (has_trans) {
                // Enable: insert empty entries so the fields appear
                elem->name_langs[mut_state.active_secondary_lang] = "";
                elem->description_langs[mut_state.active_secondary_lang] = "";
                if (has_content) {
                    elem->content_langs[mut_state.active_secondary_lang] = "";
                }
            } else {
                // Disable: remove the language entries
                elem->name_langs.erase(mut_state.active_secondary_lang);
                elem->description_langs.erase(mut_state.active_secondary_lang);
                elem->content_langs.erase(mut_state.active_secondary_lang);
            }
            modified = true;
        }
    }

    if (confidence_callbacks && confidence_callbacks->model_for_element) {
        ConfidencePanelModel confidence_model = confidence_callbacks->model_for_element(*elem);
        ConfidencePanelCallbacks panel_callbacks;
        panel_callbacks.add_confidence = [&](ConfidenceInputMode mode) {
            if (!confidence_callbacks->save_confidence)
                return false;
            ElementConfidence confidence;
            confidence.enabled = true;
            confidence.mode = mode;
            const bool element_changed = confidence_callbacks->save_confidence(*elem, confidence);
            modified = modified || element_changed;
            return element_changed;
        };
        panel_callbacks.save_confidence = [&](const ElementConfidence& confidence) {
            if (!confidence_callbacks->save_confidence)
                return false;
            const bool element_changed = confidence_callbacks->save_confidence(*elem, confidence);
            modified = modified || element_changed;
            return element_changed;
        };
        panel_callbacks.set_active = [&](bool active) {
            if (!confidence_callbacks->set_confidence_active)
                return false;
            return confidence_callbacks->set_confidence_active(*elem, active);
        };
        panel_callbacks.mark_reviewed = [&]() {
            if (!confidence_callbacks->mark_reviewed)
                return false;
            return confidence_callbacks->mark_reviewed(*elem);
        };
        panel_callbacks.backup_invalid_and_reset = confidence_callbacks->backup_invalid_and_reset;
        ShowConfidencePanel(confidence_model, panel_callbacks);
    }

    if (read_only) {
        ImGui::EndDisabled();
        // BeginDisabled silently swallows widget interactions, but inputs may
        // still have produced spurious `modified` flags via in-place buffer
        // edits earlier. Force `modified` off so no sync to SACM happens.
        modified = false;
    }

    if (history_callbacks && history_callbacks->model_for_element) {
        ElementHistoryModel hm = history_callbacks->model_for_element(elem->id);
        if (hm.available) {
            InspectorSection(ICON_FA_HISTORY, AF_TR("History"));
            if (!hm.ever_seen) {
                ImGui::TextDisabled("%s", AF_TR("No recorded changes for this element.").c_str());
            } else {
                const std::string last_changed_value =
                    hm.last_changed_at.empty()
                        ? std::string("-")
                        : (hm.last_changed_by.empty()
                               ? hm.last_changed_at
                               : ui::i18n::trf("{0}  by {1}", hm.last_changed_at, hm.last_changed_by));
                RenderMetadataRow(AF_TR("Last changed").c_str(), last_changed_value);
                RenderMetadataRow(AF_TR("Changes").c_str(), std::to_string(hm.change_count));
                if (hm.has_baseline) {
                    const std::string baseline_value =
                        (hm.changed_since_baseline ? AF_TR("Yes") : AF_TR("No")) +
                        (hm.baseline_label.empty() ? "" : "  (" + hm.baseline_label + ")");
                    RenderMetadataRow(AF_TR("Changed since baseline").c_str(), baseline_value);
                }
            }
            ImGui::Spacing();
            if (ImGui::SmallButton(AF_TR("View Element History").c_str()) && history_callbacks->open_element_history)
                history_callbacks->open_element_history(elem->id);
            ImGui::SameLine();
            ImGui::BeginDisabled(true);
            ImGui::SmallButton(AF_TR("Compare to Baseline").c_str());
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", AF_TR("Available in a later release.").c_str());
        }
    }

    // Sync edits to SACM model
    if (modified) {
        sync_to_sacm(sacm_pkg,
                     elem->id,
                     elem->name,
                     elem->description,
                     elem->content,
                     elem->undeveloped,
                     elem->name_langs,
                     elem->description_langs,
                     elem->content_langs);
    }

    ImGui::PopID(); // element ID scope
    return modified;
}

} // namespace ui::panels
