#pragma once

#include "sacm/sacm_model.h"

#include <string>
#include <vector>

namespace core {

struct TerminologyPackageRef {
    std::string id;
    std::string gid;
};

struct TerminologyPackageCreateResult {
    bool success = false;
    TerminologyPackageRef package_ref;
    std::string error;
};

struct TerminologyTermRef {
    std::string id;
    std::string gid;
};

struct TerminologyTermDraft {
    std::string value;
    std::string name;
    std::string description;
    std::vector<std::string> category_refs;
    std::string externalReference;
    std::string origin;
};

struct TerminologyTermCreateResult {
    bool success = false;
    TerminologyTermRef term_ref;
    std::string error;
};

enum class TerminologyTermIssueSeverity { Error, Warning, Info };

struct TerminologyTermIssue {
    TerminologyTermRef term_ref;
    TerminologyTermIssueSeverity severity = TerminologyTermIssueSeverity::Info;
    std::string message;
};

struct TerminologyTermUsageSummary {
    TerminologyTermRef term_ref;
    int count = 0;
};

sacm::TerminologyPackage* FindTerminologyPackage(sacm::AssuranceCasePackage& package,
                                                 const TerminologyPackageRef& package_ref);
const sacm::TerminologyPackage* FindTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                                       const TerminologyPackageRef& package_ref);

TerminologyPackageCreateResult
CreateTerminologyPackage(sacm::AssuranceCasePackage& package, const std::string& name, const std::string& description);

bool UpdateTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              const std::string& name,
                              const std::string& description,
                              std::string& out_error);

bool CanDeleteTerminologyPackage(const sacm::TerminologyPackage& package, std::string& out_reason);

bool DeleteTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              std::string& out_error);

sacm::Term* FindTerminologyTerm(sacm::TerminologyPackage& package, const TerminologyTermRef& term_ref);
const sacm::Term* FindTerminologyTerm(const sacm::TerminologyPackage& package, const TerminologyTermRef& term_ref);
sacm::Term* FindTerminologyTerm(sacm::AssuranceCasePackage& package,
                                const TerminologyPackageRef& package_ref,
                                const TerminologyTermRef& term_ref);
const sacm::Term* FindTerminologyTerm(const sacm::AssuranceCasePackage& package,
                                      const TerminologyPackageRef& package_ref,
                                      const TerminologyTermRef& term_ref);

TerminologyTermCreateResult CreateTerminologyTerm(sacm::AssuranceCasePackage& package,
                                                  const TerminologyPackageRef& package_ref,
                                                  const TerminologyTermDraft& draft);

bool UpdateTerminologyTerm(sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref,
                           const TerminologyTermDraft& draft,
                           std::string& out_error);

bool DeleteTerminologyTerm(sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref,
                           std::string& out_error);

std::vector<TerminologyTermIssue> ValidateTerminologyTerms(const sacm::TerminologyPackage& package);
int CountTerminologyTermUsage(const sacm::AssuranceCasePackage& package, const sacm::Term& term);
std::vector<TerminologyTermUsageSummary> BuildTerminologyTermUsageSummaries(
    const sacm::AssuranceCasePackage& package, const sacm::TerminologyPackage& terminology_package);

} // namespace core