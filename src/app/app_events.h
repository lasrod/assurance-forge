#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace app {

struct StatusMessageEvent {
    std::string message;
};

struct TreeDirtyEvent {
    bool dirty = true;
    bool focus_root = false;
};

struct DocumentDirtyEvent {
    bool dirty = true;
    bool mark_app_dirty = true;
};

struct ReviewItemsDirtyEvent {
    bool dirty = true;
    bool mark_app_dirty = true;
};

struct ProjectFilesChangedEvent {};

struct ActiveModelChangedEvent {
    bool focus_root = false;
};

struct SelectionChangedEvent {
    std::string element_id;
    bool center_on_selection = false;
};

enum class CenterViewRequest {
    Preserve,
    GsnCanvas,
    CseRegister,
    EvidenceRegister,
};

struct CenterRequestEvent {
    CenterViewRequest view = CenterViewRequest::Preserve;
    bool center_on_selection = false;
    bool center_on_marked = false;
    bool force_tab_selection = false;
};

struct ProposalModeChangedEvent {
    bool creator_active = false;
    bool preview_active = false;
    bool clear_highlights = false;
};

struct ProposalHighlightEvent {
    std::unordered_set<std::string> highlight_ids;
    std::unordered_set<std::string> marked_for_removal;
    bool dim_non_proposal_nodes = false;
    bool center_on_marked = false;
};

enum class ModalKind {
    NotImplemented,
    RemoveConfirm,
    DeleteReviewItemConfirm,
    CreateProject,
    ProjectFileName,
    ProjectLoadReport,
    SaveBeforeExit,
    StartupProject,
    ReviewerNamePrompt,
    AiReviewDebug,
    Preferences,
    ThemeTweaks,
};

struct ModalRequestEvent {
    ModalKind kind = ModalKind::NotImplemented;
    bool open = true;
    std::string message;
};

using AppEvent = std::variant<StatusMessageEvent,
                              TreeDirtyEvent,
                              DocumentDirtyEvent,
                              ReviewItemsDirtyEvent,
                              ProjectFilesChangedEvent,
                              ActiveModelChangedEvent,
                              SelectionChangedEvent,
                              CenterRequestEvent,
                              ProposalModeChangedEvent,
                              ProposalHighlightEvent,
                              ModalRequestEvent>;

class AppEvents {
public:
    using SubscriptionId = size_t;

    template <typename EventT, typename ListenerT>
    SubscriptionId Subscribe(ListenerT&& listener) {
        std::function<void(const EventT&)> typed_listener(std::forward<ListenerT>(listener));
        const SubscriptionId id = next_subscription_id_++;
        listeners_.push_back(Subscription{
            id,
            [typed_listener = std::move(typed_listener)](const AppEvent& event) {
                if (const EventT* typed_event = std::get_if<EventT>(&event)) {
                    typed_listener(*typed_event);
                }
            },
        });
        return id;
    }

    void Unsubscribe(SubscriptionId id) {
        listeners_.erase(std::remove_if(listeners_.begin(),
                                        listeners_.end(),
                                        [id](const Subscription& listener) { return listener.id == id; }),
                         listeners_.end());
    }

    template <typename EventT>
    void Emit(const EventT& event) const {
        Emit(AppEvent(event));
    }

    void Emit(const AppEvent& event) const {
        for (const Subscription& listener : listeners_) {
            listener.callback(event);
        }
    }

    void Clear() {
        listeners_.clear();
    }

private:
    struct Subscription {
        SubscriptionId id = 0;
        std::function<void(const AppEvent&)> callback;
    };

    SubscriptionId next_subscription_id_ = 1;
    std::vector<Subscription> listeners_;
};

} // namespace app