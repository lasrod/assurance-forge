#pragma once

// SACM data-model PODs used across parser, core, and ui layers.
//
// These types previously lived in `parser::xml_parser.h`, which forced every
// UI header that referenced a `SacmElement` or `AssuranceCase` to include the
// parser implementation header. Hosting them in `core::` keeps the UI layer
// free of parser-layer dependencies while parser/* keeps backwards-compatible
// type aliases (see parser/xml_parser.h).

#include <map>
#include <string>
#include <vector>

namespace core {

// SACM has storage identity (`id` / `gid`) but no native GSN notation
// identifier. Assurance Forge preserves that independent GSN value in a
// vendor TaggedValue while keeping all graph references on the SACM id.
inline constexpr char kGsnIdentifierTagKey[] = "assuranceForge.gsn.identifier";

// Assurance Claim Points are a GSN v3 concept SACM 2.3 has no class for, so an
// ACP is stored as a set of vendor TaggedValues on the element or relationship it
// annotates: a marker tag whose value is the ACP id, plus one
// `assuranceForge.acp.<id>.<field>` tag per populated field. A confidence
// argument tree is an ordinary ArgumentPackage wearing a purpose tag.
//
// They live here rather than inside `core::acp` because the library seam has to
// write exactly the keys the projection reads, and two copies of a key string is
// how a projection quietly stops seeing what an editor writes.
inline constexpr char kAcpMarkerTagKey[] = "assuranceForge.acp";
inline constexpr char kAcpFieldTagPrefix[] = "assuranceForge.acp.";
inline constexpr char kArgumentPackagePurposeTagKey[] = "assuranceForge.argumentPackage.purpose";
inline constexpr char kArgumentPackagePurposeConfidence[] = "confidence";

// Represents a SACM element (claim, strategy, evidence, etc.)
struct SacmElement {
    std::string id;
    std::string gid;
    std::string gsn_identifier;
    std::string name;
    std::string type; // "claim", "argumentreasoning", "artifact", etc. (lowercased local-name)
    std::string content;
    std::string description;
    bool undeveloped = false;
    // SACMElement::isAbstract, used by GSN pattern notation to mean the
    // element remains to be instantiated.
    bool is_abstract = false;

    // Multi-language maps: lang code -> text (e.g. "en" -> "...", "ja" -> "...")
    std::map<std::string, std::string> name_langs;
    std::map<std::string, std::string> description_langs;
    std::map<std::string, std::string> content_langs;

    // Relationship fields (populated for assertedinference, assertedcontext, assertedevidence)
    std::vector<std::string> source_refs;     // ids from <source ref="..."/>
    std::vector<std::string> target_refs;     // ids from <target ref="..."/>
    std::vector<std::string> meta_claim_refs; // ids from <metaClaim ref="..."/>
    std::string reasoning_ref;                // from reasoning attribute (assertedinference)
    std::string structure_ref;                // from structure attribute (argumentreasoning, 11.12)
    std::string assertion_declaration;        // from assertionDeclaration attribute
    bool is_counter = false;                  // from isCounter attribute (GSN dialectic challenge)

    // Terminology fields (SACM clause 10.7), populated for `term` elements only.
    // A term's value is carried in `content` and its definition in `description`,
    // like any other element's text; these are the three fields a term has that
    // nothing else does. They are here rather than only in the legacy
    // `sacm::Term` because the whole edit pipeline -- proposals, drafts, diffs,
    // semantic hashes -- runs on this POD, so a field it cannot hold is a field
    // no proposal can change.
    std::vector<std::string> category_refs; // ids of the Categories classifying this term (10.8)
    std::string external_reference;         // citation string: a URL, standard clause, or document
    std::string origin_ref;                 // id of the element this term's definition comes from

    // Evidence provenance, populated for `artifactreference` elements only: the
    // first ArtifactElement the reference cites (clause 11.9) and, when the
    // reference cites a Resource, that Resource's location (clause 12.12) --
    // the path or URL where the evidence can be found. Here for the reason the
    // term fields are: the register reads this POD, and a draft or proposal can
    // only change a field it carries.
    std::string referenced_artifact_id;
    std::string artifact_location;
};

struct AcpRecord {
    std::string id;
    std::string name;
    std::string target_kind; // "relationship" or "element"
    std::string target_id;
    std::string resolution_kind; // "none", "text", or "topGoalReference"
    std::string text;
    std::string confidence_claim_id;
    std::string argument_package_id;
    std::string top_goal_id;
};

// Represents a parsed SACM assurance case
struct AssuranceCase {
    std::string id;
    std::string name;
    std::string description;
    std::vector<SacmElement> elements;
    std::vector<AcpRecord> acps;
};

// True for the element kinds whose single clause-8.9 Description IS their
// statement, which this POD carries in `content` (ADR 0012). Such an element has
// no note, so its `description` is always empty and nothing may write one.
//
// It lives here, beside the type, because the same question is asked from three
// layers -- the projection that fills these fields, the diff that compares them,
// and the patch service that refuses to write them -- and three copies of the
// kind list is how they drift apart.
inline bool ClaimLikeCarriesStatementAsDescription(const SacmElement& element) {
    return element.type == "claim" || element.type == "argumentreasoning";
}

// True for the element kinds that actually HAVE a `content`: a claim-like
// element's statement (clause 8.9) and a Term's or Expression's value (clause
// 10.11). Every other kind -- an artifact reference, a relationship, a package --
// keeps `content` permanently empty: the projection never fills it, and the
// promotion seam has no library operation to write it.
//
// The companion of the rule above, and it exists for the same reason. A POD
// `SacmElement` carries name/content/description for every kind, so writing
// `content` on a context succeeds against this struct, survives materialization,
// and renders in the working draft -- then fails at promotion, where the seam
// refuses, leaving a draft that can be seen and never accepted. Asking this
// question while an operation is being STAGED is what keeps the flat model from
// admitting a change the document cannot hold.
inline bool ElementCarriesContent(const SacmElement& element) {
    return ClaimLikeCarriesStatementAsDescription(element) || element.type == "term" || element.type == "expression";
}

} // namespace core

// Transitional aliases: the SACM POD types historically lived in
// `namespace parser`. Until call sites are renamed to `core::Xxx`, expose the
// old spellings so consumers that previously included `parser/xml_parser.h`
// can switch to this header without source-wide renames.
namespace parser {
using SacmElement = core::SacmElement;
using AcpRecord = core::AcpRecord;
using AssuranceCase = core::AssuranceCase;
} // namespace parser
