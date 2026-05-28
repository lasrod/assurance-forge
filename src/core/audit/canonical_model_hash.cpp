#include "core/audit/canonical_model_hash.h"

#include "core/sha256.h"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <vector>

namespace core::audit {

namespace {

// Stable identity key for a SACM element. Prefer gid (globally unique by
// design); fall back to id (locally unique within the file). If both are
// missing we still produce a deterministic key based on element type +
// content hash but those cases are pathological.
std::string StableKey(const sacm::SacmElement& element) {
    if (!element.gid.empty())
        return "g:" + element.gid;
    if (!element.id.empty())
        return "i:" + element.id;
    return "x:" + Sha256::HexDigest(element.name + "|" + element.description).substr(0, 16);
}

// Normalize line endings (\r\n, \r → \n) so platform-specific whitespace
// does not affect the canonical hash.
std::string NormalizeLineEndings(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Length-prefixed field encoding: `key:<len>:<value>\n`. Length-prefixing
// makes the encoding unambiguous regardless of any characters appearing in
// the value (newlines, colons, etc.).
void AppendField(std::ostringstream& out, std::string_view key, const std::string& raw_value) {
    std::string value = NormalizeLineEndings(raw_value);
    out << key << ':' << value.size() << ':' << value << '\n';
}

void AppendField(std::ostringstream& out, std::string_view key, bool value) {
    AppendField(out, key, value ? std::string("true") : std::string("false"));
}

void AppendMultiLang(std::ostringstream& out, std::string_view key, const sacm::MultiLangText& text) {
    // Sort languages so map iteration order (already sorted in std::map) is
    // explicit and survives type changes.
    std::vector<std::pair<std::string, std::string>> entries(text.texts.begin(), text.texts.end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    out << key << "_n:" << entries.size() << '\n';
    for (const auto& [lang, value] : entries) {
        AppendField(out, std::string(key) + "[" + lang + "]", value);
    }
}

void AppendTaggedValues(std::ostringstream& out, const std::vector<sacm::TaggedValue>& tvs) {
    std::vector<sacm::TaggedValue> sorted = tvs;
    std::sort(sorted.begin(), sorted.end(), [](const sacm::TaggedValue& a, const sacm::TaggedValue& b) {
        if (a.key != b.key)
            return a.key < b.key;
        if (a.value != b.value)
            return a.value < b.value;
        return a.id < b.id;
    });
    out << "tv_n:" << sorted.size() << '\n';
    for (const sacm::TaggedValue& tv : sorted) {
        AppendField(out, "tv.key", tv.key);
        AppendField(out, "tv.value", tv.value);
    }
}

void AppendBaseElement(std::ostringstream& out, std::string_view type_tag, const sacm::SacmElement& element) {
    out << "==" << type_tag << "==\n";
    AppendField(out, "stable_key", StableKey(element));
    AppendField(out, "gid", element.gid);
    AppendField(out, "name", element.name);
    AppendField(out, "description", element.description);
    AppendMultiLang(out, "name_ml", element.name_ml);
    AppendMultiLang(out, "description_ml", element.description_ml);
    AppendField(out, "isCitation", element.isCitation);
    AppendField(out, "isAbstract", element.isAbstract);
    AppendField(out, "citedElement", element.citedElement);
    AppendField(out, "abstractForm", element.abstractForm);
    AppendTaggedValues(out, element.taggedValues);
}

template <typename T>
std::vector<const T*> SortedByStableKey(const std::vector<T>& items) {
    std::vector<const T*> sorted;
    sorted.reserve(items.size());
    for (const T& item : items)
        sorted.push_back(&item);
    std::sort(sorted.begin(), sorted.end(),
              [](const T* a, const T* b) { return StableKey(*a) < StableKey(*b); });
    return sorted;
}

std::vector<std::string> SortedCopy(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return v;
}

void AppendRefList(std::ostringstream& out, std::string_view key, const std::vector<std::string>& refs) {
    std::vector<std::string> sorted = SortedCopy(refs);
    out << key << "_n:" << sorted.size() << '\n';
    for (const std::string& ref : sorted) {
        AppendField(out, std::string(key) + "[]", ref);
    }
}

void AppendClaim(std::ostringstream& out, const sacm::Claim& claim) {
    AppendBaseElement(out, "CLAIM", claim);
    AppendField(out, "content", claim.content);
    AppendMultiLang(out, "content_ml", claim.content_ml);
    AppendField(out, "assertionDeclaration", claim.assertionDeclaration);
    AppendField(out, "undeveloped", claim.undeveloped);
}

void AppendReasoning(std::ostringstream& out, const sacm::ArgumentReasoning& reasoning) {
    AppendBaseElement(out, "REASONING", reasoning);
    AppendField(out, "content", reasoning.content);
    AppendMultiLang(out, "content_ml", reasoning.content_ml);
    AppendField(out, "undeveloped", reasoning.undeveloped);
}

void AppendArtifactReference(std::ostringstream& out, const sacm::ArtifactReference& ref) {
    AppendBaseElement(out, "ARTIFACT_REF", ref);
    AppendField(out, "referencedArtifact", ref.referencedArtifact);
}

void AppendRelationship(std::ostringstream& out,
                        std::string_view type_tag,
                        const sacm::AssertedRelationship& rel,
                        const std::string& reasoning) {
    AppendBaseElement(out, type_tag, rel);
    AppendRefList(out, "sources", rel.sources);
    AppendRefList(out, "targets", rel.targets);
    AppendRefList(out, "metaClaims", rel.metaClaims);
    AppendField(out, "assertionDeclaration", rel.assertionDeclaration);
    AppendField(out, "isCounter", rel.isCounter);
    if (!reasoning.empty() || type_tag == "ASSERTED_INFERENCE")
        AppendField(out, "reasoning", reasoning);
}

void AppendArtifact(std::ostringstream& out, const sacm::Artifact& artifact) {
    AppendBaseElement(out, "ARTIFACT", artifact);
    AppendField(out, "version", artifact.version);
    AppendField(out, "date", artifact.date);
}

void AppendCategory(std::ostringstream& out, const sacm::Category& cat) {
    AppendBaseElement(out, "CATEGORY", cat);
}

void AppendExpression(std::ostringstream& out, const sacm::Expression& expr) {
    AppendBaseElement(out, "EXPRESSION", expr);
    AppendField(out, "value", expr.value);
}

void AppendTerm(std::ostringstream& out, const sacm::Term& term) {
    AppendBaseElement(out, "TERM", term);
    AppendField(out, "value", term.value);
    AppendField(out, "externalReference", term.externalReference);
    AppendField(out, "origin", term.origin);
    AppendRefList(out, "categories", term.category_refs);
}

void AppendTerminologyPackage(std::ostringstream& out, const sacm::TerminologyPackage& tp) {
    AppendBaseElement(out, "TERMINOLOGY_PACKAGE", tp);
    auto cats = SortedByStableKey(tp.categories);
    out << "categories_n:" << cats.size() << '\n';
    for (const sacm::Category* c : cats)
        AppendCategory(out, *c);
    auto exprs = SortedByStableKey(tp.expressions);
    out << "expressions_n:" << exprs.size() << '\n';
    for (const sacm::Expression* e : exprs)
        AppendExpression(out, *e);
    auto terms = SortedByStableKey(tp.terms);
    out << "terms_n:" << terms.size() << '\n';
    for (const sacm::Term* t : terms)
        AppendTerm(out, *t);
}

void AppendArtifactPackage(std::ostringstream& out, const sacm::ArtifactPackage& ap) {
    AppendBaseElement(out, "ARTIFACT_PACKAGE", ap);
    auto items = SortedByStableKey(ap.artifacts);
    out << "artifacts_n:" << items.size() << '\n';
    for (const sacm::Artifact* a : items)
        AppendArtifact(out, *a);
}

void AppendArgumentPackage(std::ostringstream& out, const sacm::ArgumentPackage& ap) {
    AppendBaseElement(out, "ARGUMENT_PACKAGE", ap);

    auto tps = SortedByStableKey(ap.terminologyPackages);
    out << "terminologyPackages_n:" << tps.size() << '\n';
    for (const sacm::TerminologyPackage* tp : tps)
        AppendTerminologyPackage(out, *tp);

    auto claims = SortedByStableKey(ap.claims);
    out << "claims_n:" << claims.size() << '\n';
    for (const sacm::Claim* c : claims)
        AppendClaim(out, *c);

    auto reasonings = SortedByStableKey(ap.argumentReasonings);
    out << "argumentReasonings_n:" << reasonings.size() << '\n';
    for (const sacm::ArgumentReasoning* r : reasonings)
        AppendReasoning(out, *r);

    auto refs = SortedByStableKey(ap.artifactReferences);
    out << "artifactReferences_n:" << refs.size() << '\n';
    for (const sacm::ArtifactReference* r : refs)
        AppendArtifactReference(out, *r);

    auto infs = SortedByStableKey(ap.assertedInferences);
    out << "assertedInferences_n:" << infs.size() << '\n';
    for (const sacm::AssertedInference* r : infs)
        AppendRelationship(out, "ASSERTED_INFERENCE", *r, r->reasoning);

    auto ctxs = SortedByStableKey(ap.assertedContexts);
    out << "assertedContexts_n:" << ctxs.size() << '\n';
    for (const sacm::AssertedContext* r : ctxs)
        AppendRelationship(out, "ASSERTED_CONTEXT", *r, {});

    auto evs = SortedByStableKey(ap.assertedEvidences);
    out << "assertedEvidences_n:" << evs.size() << '\n';
    for (const sacm::AssertedEvidence* r : evs)
        AppendRelationship(out, "ASSERTED_EVIDENCE", *r, {});
}

} // namespace

std::string CanonicalModelText(const sacm::AssuranceCasePackage& package) {
    std::ostringstream out;
    out << "CanonicalModelHash/v" << kCanonicalModelHashVersion << '\n';
    AppendBaseElement(out, "ASSURANCE_CASE_PACKAGE", package);

    auto tps = SortedByStableKey(package.terminologyPackages);
    out << "terminologyPackages_n:" << tps.size() << '\n';
    for (const sacm::TerminologyPackage* tp : tps)
        AppendTerminologyPackage(out, *tp);

    auto aps = SortedByStableKey(package.artifactPackages);
    out << "artifactPackages_n:" << aps.size() << '\n';
    for (const sacm::ArtifactPackage* ap : aps)
        AppendArtifactPackage(out, *ap);

    auto args = SortedByStableKey(package.argumentPackages);
    out << "argumentPackages_n:" << args.size() << '\n';
    for (const sacm::ArgumentPackage* ap : args)
        AppendArgumentPackage(out, *ap);

    return out.str();
}

std::string CanonicalModelHash(const sacm::AssuranceCasePackage& package) {
    return Sha256::HexDigest(CanonicalModelText(package));
}

} // namespace core::audit
