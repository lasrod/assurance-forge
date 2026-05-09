#pragma once

#include "sacm/sacm_model.h"

#include <string>

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

sacm::TerminologyPackage* FindTerminologyPackage(sacm::AssuranceCasePackage& package,
                                                 const TerminologyPackageRef& package_ref);
const sacm::TerminologyPackage* FindTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                                       const TerminologyPackageRef& package_ref);

TerminologyPackageCreateResult CreateTerminologyPackage(sacm::AssuranceCasePackage& package,
                                                        const std::string& name,
                                                        const std::string& description);

bool UpdateTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              const std::string& name,
                              const std::string& description,
                              std::string& out_error);

bool CanDeleteTerminologyPackage(const sacm::TerminologyPackage& package, std::string& out_reason);

bool DeleteTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              std::string& out_error);

} // namespace core