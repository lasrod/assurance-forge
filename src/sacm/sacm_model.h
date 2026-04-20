#pragma once

#include <string>
#include <vector>

namespace sacm {

// ===== Base element =====

struct SacmElement {
    std::string id;
    std::string name;
    std::string description;
};

// ===== Terminology =====

struct Expression : SacmElement {
    std::string value;
};

struct TerminologyPackage : SacmElement {
    std::vector<Expression> expressions;
};

// ===== Artifacts =====

struct Artifact : SacmElement {
};

struct ArtifactPackage : SacmElement {
    std::vector<Artifact> artifacts;
};

// ===== Argumentation elements =====

struct Claim : SacmElement {
    std::string content;
    std::string assertionDeclaration;  // "asserted", "needsSupport", "assumed", "defeated"
};

struct ArgumentReasoning : SacmElement {
    std::string content;
};

struct ArtifactReference : SacmElement {
    std::string referencedArtifact;  // id of the referenced Artifact
};

// ===== Relationships =====

struct AssertedRelationship : SacmElement {
    std::vector<std::string> sources;  // ref ids
    std::vector<std::string> targets;  // ref ids
    std::string assertionDeclaration;
};

struct AssertedInference : AssertedRelationship {
    std::string reasoning;  // ref id to ArgumentReasoning
};

struct AssertedContext : AssertedRelationship {
};

struct AssertedEvidence : AssertedRelationship {
};

// ===== Argument package =====

struct ArgumentPackage : SacmElement {
    std::vector<Claim> claims;
    std::vector<ArgumentReasoning> argumentReasonings;
    std::vector<ArtifactReference> artifactReferences;
    std::vector<AssertedInference> assertedInferences;
    std::vector<AssertedContext> assertedContexts;
    std::vector<AssertedEvidence> assertedEvidences;
};

// ===== Top-level package =====

struct AssuranceCasePackage : SacmElement {
    std::string namespace_prefix;  // e.g. "sacm" or "SACM"
    std::string namespace_uri;     // e.g. "http://www.omg.org/spec/SACM/2.2/Argumentation"

    std::vector<TerminologyPackage> terminologyPackages;
    std::vector<ArtifactPackage> artifactPackages;
    std::vector<ArgumentPackage> argumentPackages;
};

}  // namespace sacm
