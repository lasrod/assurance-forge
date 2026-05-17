#include "core/sacm_identity.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <unordered_set>

namespace core {
namespace {

std::string HexGroup(std::mt19937_64& rng, int width) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string value;
    value.reserve(static_cast<size_t>(width));
    for (int i = 0; i < width; ++i)
        value.push_back(kHexDigits[dist(rng)]);
    return value;
}

std::string UuidVariantGroup(std::mt19937_64& rng) {
    static constexpr char kVariantDigits[] = "89ab";
    std::uniform_int_distribution<int> variant_dist(0, 3);
    std::string value;
    value.reserve(4);
    value.push_back(kVariantDigits[variant_dist(rng)]);
    value += HexGroup(rng, 3);
    return value;
}

template <typename T>
bool SetGidById(std::vector<T>& values, const std::string& id, const std::string& gid) {
    for (T& value : values) {
        if (value.id == id) {
            value.gid = gid;
            return true;
        }
    }
    return false;
}

bool SetPackageElementGid(sacm::AssuranceCasePackage& package, const std::string& id, const std::string& gid) {
    if (package.id == id) {
        package.gid = gid;
        return true;
    }
    for (sacm::TerminologyPackage& terminology_package : package.terminologyPackages) {
        if (terminology_package.id == id) {
            terminology_package.gid = gid;
            return true;
        }
        if (SetGidById(terminology_package.categories, id, gid) || SetGidById(terminology_package.expressions, id, gid) ||
            SetGidById(terminology_package.terms, id, gid)) {
            return true;
        }
    }
    for (sacm::ArtifactPackage& artifact_package : package.artifactPackages) {
        if (artifact_package.id == id) {
            artifact_package.gid = gid;
            return true;
        }
        if (SetGidById(artifact_package.artifacts, id, gid))
            return true;
    }
    for (sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        if (argument_package.id == id) {
            argument_package.gid = gid;
            return true;
        }
        if (SetGidById(argument_package.claims, id, gid) ||
            SetGidById(argument_package.argumentReasonings, id, gid) ||
            SetGidById(argument_package.artifactReferences, id, gid) ||
            SetGidById(argument_package.assertedInferences, id, gid) ||
            SetGidById(argument_package.assertedContexts, id, gid) ||
            SetGidById(argument_package.assertedEvidences, id, gid)) {
            return true;
        }
        for (sacm::TerminologyPackage& terminology_package : argument_package.terminologyPackages) {
            if (terminology_package.id == id) {
                terminology_package.gid = gid;
                return true;
            }
            if (SetGidById(terminology_package.categories, id, gid) ||
                SetGidById(terminology_package.expressions, id, gid) || SetGidById(terminology_package.terms, id, gid)) {
                return true;
            }
        }
    }
    return false;
}

std::unordered_set<std::string> ExistingGids(const parser::AssuranceCase& model) {
    std::unordered_set<std::string> gids;
    for (const parser::SacmElement& element : model.elements) {
        if (!element.gid.empty())
            gids.insert(element.gid);
    }
    return gids;
}

} // namespace

std::string GenerateSacmGid() {
    static std::mt19937_64 rng{static_cast<unsigned long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    return HexGroup(rng, 8) + "-" + HexGroup(rng, 4) + "-4" + HexGroup(rng, 3) + "-" + UuidVariantGroup(rng) + "-" +
           HexGroup(rng, 12);
}

EnsureGidResult EnsureElementGid(parser::AssuranceCase& model,
                                 sacm::AssuranceCasePackage* package,
                                 parser::SacmElement& element,
                                 std::string& error) {
    error.clear();
    if (!element.gid.empty())
        return EnsureGidResult::AlreadyPresent;

    std::unordered_set<std::string> gids = ExistingGids(model);
    std::string gid;
    do {
        gid = GenerateSacmGid();
    } while (gids.find(gid) != gids.end());

    if (package && !SetPackageElementGid(*package, element.id, gid)) {
        error = "Could not assign generated gid to the SACM model element.";
        return EnsureGidResult::Failed;
    }
    element.gid = gid;
    return EnsureGidResult::Generated;
}

} // namespace core