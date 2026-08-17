#include "app/areas/staged_finding_text.h"

#include "ui/i18n/localization.h"

namespace app::areas {

std::string StagedFindingText(const core::sccg::StagedFinding& finding) {
    // Keyed by check id, not guideline id: AR.1 backs two different findings
    // (a solution with children, and a support cycle) that need two different
    // sentences.
    if (finding.check_id == "check-evidence-trace") {
        return AF_TR("This claim has no support and is not marked undeveloped, so a reviewer cannot "
                     "tell whether evidence is missing or still to come. Give it support, or mark "
                     "it undeveloped to say so deliberately.");
    }
    if (finding.check_id == "check-explicit-strategy") {
        return AF_TR("This strategy develops into nothing. A decomposition step that produces no "
                     "sub-claims states an inference the argument never makes.");
    }
    if (finding.check_id == "check-element-role-misuse") {
        return AF_TR("This is a solution -- the artefact or observation the argument rests on -- so "
                     "it should be a leaf. Elements hanging beneath it are not carrying the role "
                     "the structure says they are.");
    }
    if (finding.check_id == "check-circular-support") {
        return AF_TR("These operations put a claim in its own support chain, so the argument supports "
                     "itself and establishes nothing.");
    }
    if (finding.check_id == "check-bounded-qualifiers" && !finding.params.empty()) {
        return ui::i18n::trf("This claim uses \"{0}\", which SCCG names as a term needing bounds. Say what "
                             "it means here -- against which hazards, in which operating conditions, to "
                             "what standard -- in the claim or in attached context.",
                             finding.params[0]);
    }
    // Unknown check: English is better than nothing, and the fallback showing
    // up in a review is the signal that a template is missing here.
    return finding.detail;
}

} // namespace app::areas
