#pragma once

#include <string>
#include <vector>
#include <map>

// =============================================================================
// SACM domain model
// -----------------------------------------------------------------------------
// In-memory representation used by the parser and the serializer. Maps a
// useful subset of the OMG Structured Assurance Case Metamodel (SACM) v2.3
// (formal/23-05-08).
//
// SACM 2.3 elements supported here:
//   - Assurance case packaging:  AssuranceCasePackage (clause 9.2)
//   - Argumentation:             ArgumentPackage (11.4), Claim (11.11),
//                                ArgumentReasoning (11.12), ArtifactReference
//                                (11.9), AssertedInference (11.14),
//                                AssertedEvidence (11.15), AssertedContext
//                                (11.16). Counter-relationships via the
//                                inherited isCounter flag (11.13).
//   - Terminology (minimal):     TerminologyPackage (10.4), Expression (10.10).
//   - Artifacts (minimal):       ArtifactPackage (12.2), Artifact (12.7).
//   - Common base attributes:    gid, isCitation, isAbstract, citedElement,
//                                abstractForm (SACMElement, clause 8.2).
//
// SACM 2.3 features intentionally NOT modeled yet (parser only round-trips
// the fields listed above; unknown XML is dropped):
//   - PackageInterface / PackageBinding for any package type (9.3, 9.4, 11.5,
//     11.6, 12.4, 12.5, 10.5, 10.6).
//   - ArgumentGroup / ArtifactGroup / TerminologyGroup (11.2, 12.3, 10.3).
//   - Assertion::metaClaim association (11.10).
//   - AssertedArtifactSupport (11.17), AssertedArtifactContext (11.18).
//   - Full Artifact metamodel: Property, Event, Resource, Activity, Technique,
//     Participant, ArtifactAssetRelationship (12.8 - 12.14).
//   - Full Terminology metamodel: Term, Category, ExpressionElement,
//     TerminologyAsset, externalReference, origin (10.7 - 10.11).
//   - UtilityElement attachments: Note, TaggedValue, ImplementationConstraint
//     (8.7 - 8.12).
//   - SACM UML Profile (Annex F).
// =============================================================================

namespace sacm {

// ===== Multi-language text =====

// Stores text in multiple languages, keyed by language code (e.g. "en", "ja").
// Used for SACM MultiLangString (clause 8.5) and for content fields.
struct MultiLangText {
    std::map<std::string, std::string> texts;  // lang -> text

    // Get text for a given language, falling back to "en", then first available.
    std::string get(const std::string& lang) const {
        auto it = texts.find(lang);
        if (it != texts.end()) return it->second;
        it = texts.find("en");
        if (it != texts.end()) return it->second;
        if (!texts.empty()) return texts.begin()->second;
        return {};
    }

    // Get text for the default (primary) language.
    std::string get_primary() const { return get("en"); }

    // Set text for a language.
    void set(const std::string& lang, const std::string& text) { texts[lang] = text; }

    // Check if a specific language exists.
    bool has(const std::string& lang) const { return texts.count(lang) > 0; }

    // Check if any non-primary language exists with non-empty text.
    bool has_secondary() const {
        for (const auto& [lang, text] : texts) {
            if (lang != "en" && !text.empty()) return true;
        }
        return false;
    }
};

// ===== Base element (SACM SACMElement, clause 8.2) =====
//
// All SACM elements ultimately derive from SACMElement.
struct SacmElement {
    std::string id;           // XML id (xmi:id in conformant XMI)
    std::string name;         // Primary-language convenience (also in name_ml)
    std::string description;  // Primary-language convenience (also in description_ml)

    // Multi-language fields (the primary "en" text and any translations).
    MultiLangText name_ml;
    MultiLangText description_ml;

    // SACM 2.3 SACMElement attributes (clause 8.2). Optional in XML; defaults
    // match the spec defaults.
    std::string gid;             // Globally unique identifier
    bool isCitation = false;     // True if this element cites another
    bool isAbstract = false;     // True for pattern/template elements
    std::string citedElement;    // Id of the SACMElement this element cites
    std::string abstractForm;    // Id of the abstract element this conforms to
};

// ===== Terminology (SACM clause 10) =====

// Expression (10.10): a phrase with an optional structured value.
struct Expression : SacmElement {
    std::string value;
};

// TerminologyPackage (10.4): container of terminology elements.
// Only Expression is currently modeled; Term/Category/Group are TODO.
struct TerminologyPackage : SacmElement {
    std::vector<Expression> expressions;
};

// ===== Artifacts (SACM clause 12) =====

// Artifact (12.7): the distinguishable units of data used in an assurance case.
struct Artifact : SacmElement {
    std::string version;  // 12.7 attribute
    std::string date;     // 12.7 attribute (creation date, ISO 8601)
};

// ArtifactPackage (12.2): container for artifacts.
struct ArtifactPackage : SacmElement {
    std::vector<Artifact> artifacts;
};

// ===== Argumentation elements (SACM clause 11) =====

// Claim (11.11): a proposition being asserted.
// The assertionDeclaration string carries the SACM enumeration value:
//   "asserted" (default) | "needsSupport" | "assumed" | "axiomatic" |
//   "defeated" | "asCited"   (clause 11.8 AssertionDeclaration)
struct Claim : SacmElement {
    std::string content;
    std::string assertionDeclaration;
    MultiLangText content_ml;
};

// ArgumentReasoning (11.12): provides additional explanation of a relationship.
struct ArgumentReasoning : SacmElement {
    std::string content;
    MultiLangText content_ml;
};

// ArtifactReference (11.9): citation of an artifact from within an argument.
// Note: SACM 11.9 defines this as a multi-valued reference to ArtifactElement;
// we currently store a single id for simplicity.
struct ArtifactReference : SacmElement {
    std::string referencedArtifact;  // id of the referenced Artifact
};

// ===== Relationships (SACM clauses 11.13 - 11.16) =====

// AssertedRelationship (11.13): abstract base for asserted relationships.
struct AssertedRelationship : SacmElement {
    std::vector<std::string> sources;     // id references (multi-valued)
    std::vector<std::string> targets;     // id references (multi-valued)
    std::string assertionDeclaration;     // see Claim::assertionDeclaration
    bool isCounter = false;               // 11.13 attribute, default false
};

// AssertedInference (11.14): inference between assertions.
struct AssertedInference : AssertedRelationship {
    std::string reasoning;  // id reference to ArgumentReasoning
};

// AssertedContext (11.16): contextual scoping of a Claim.
struct AssertedContext : AssertedRelationship {
};

// AssertedEvidence (11.15): evidential support for a Claim.
struct AssertedEvidence : AssertedRelationship {
};

// ===== Argument package (SACM clause 11.4) =====

struct ArgumentPackage : SacmElement {
    std::vector<Claim> claims;
    std::vector<ArgumentReasoning> argumentReasonings;
    std::vector<ArtifactReference> artifactReferences;
    std::vector<AssertedInference> assertedInferences;
    std::vector<AssertedContext> assertedContexts;
    std::vector<AssertedEvidence> assertedEvidences;
};

// ===== Top-level package (SACM clause 9.2) =====

struct AssuranceCasePackage : SacmElement {
    // XML namespace metadata - preserved on round-trip so output matches input.
    std::string namespace_prefix;  // e.g. "sacm" or "SACM"
    std::string namespace_uri;     // e.g. "http://www.omg.org/spec/SACM/2.2/Argumentation"

    std::vector<TerminologyPackage> terminologyPackages;
    std::vector<ArtifactPackage> artifactPackages;
    std::vector<ArgumentPackage> argumentPackages;
};

}  // namespace sacm
