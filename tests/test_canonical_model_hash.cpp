#include "core/audit/canonical_model_hash.h"
#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

namespace {

sacm::Claim MakeClaim(const std::string& id, const std::string& name, const std::string& desc) {
    sacm::Claim c;
    c.id = id;
    c.gid = id;
    c.name = name;
    c.description = desc;
    c.assertionDeclaration = "asserted";
    return c;
}

sacm::ArgumentPackage MakePackage(const std::string& id, std::vector<sacm::Claim> claims) {
    sacm::ArgumentPackage pkg;
    pkg.id = id;
    pkg.gid = id;
    pkg.name = "P";
    pkg.claims = std::move(claims);
    return pkg;
}

sacm::AssuranceCasePackage MakeCase(std::vector<sacm::ArgumentPackage> args) {
    sacm::AssuranceCasePackage ac;
    ac.id = "ac1";
    ac.gid = "ac1";
    ac.name = "Case";
    ac.argumentPackages = std::move(args);
    return ac;
}

} // namespace

TEST(CanonicalModelHash, IsStableUnderClaimReordering) {
    auto a = MakeCase({MakePackage("p1",
                                   {MakeClaim("G1", "Goal 1", "desc 1"),
                                    MakeClaim("G2", "Goal 2", "desc 2"),
                                    MakeClaim("G3", "Goal 3", "desc 3")})});
    auto b = MakeCase({MakePackage("p1",
                                   {MakeClaim("G3", "Goal 3", "desc 3"),
                                    MakeClaim("G1", "Goal 1", "desc 1"),
                                    MakeClaim("G2", "Goal 2", "desc 2")})});

    EXPECT_EQ(core::audit::CanonicalModelHash(a), core::audit::CanonicalModelHash(b));
}

TEST(CanonicalModelHash, IsStableUnderPackageReordering) {
    auto a = MakeCase({MakePackage("pA", {MakeClaim("G1", "n", "d")}), MakePackage("pB", {MakeClaim("G2", "n", "d")})});
    auto b = MakeCase({MakePackage("pB", {MakeClaim("G2", "n", "d")}), MakePackage("pA", {MakeClaim("G1", "n", "d")})});

    EXPECT_EQ(core::audit::CanonicalModelHash(a), core::audit::CanonicalModelHash(b));
}

TEST(CanonicalModelHash, ChangesWhenClaimNameChanges) {
    auto a = MakeCase({MakePackage("p1", {MakeClaim("G1", "Original", "desc")})});
    auto b = MakeCase({MakePackage("p1", {MakeClaim("G1", "Mutated", "desc")})});

    EXPECT_NE(core::audit::CanonicalModelHash(a), core::audit::CanonicalModelHash(b));
}

TEST(CanonicalModelHash, NormalizesLineEndingsInDescriptions) {
    auto a = MakeCase({MakePackage("p1", {MakeClaim("G1", "n", "line1\nline2")})});
    auto b = MakeCase({MakePackage("p1", {MakeClaim("G1", "n", "line1\r\nline2")})});

    EXPECT_EQ(core::audit::CanonicalModelHash(a), core::audit::CanonicalModelHash(b));
}

TEST(CanonicalModelHash, SortsRelationshipSourcesAndTargets) {
    sacm::AssertedInference rel_a;
    rel_a.id = "I1";
    rel_a.gid = "I1";
    rel_a.sources = {"S1", "S2", "S3"};
    rel_a.targets = {"T1", "T2"};

    sacm::AssertedInference rel_b = rel_a;
    rel_b.sources = {"S3", "S1", "S2"};
    rel_b.targets = {"T2", "T1"};

    sacm::ArgumentPackage pa;
    pa.id = "p";
    pa.gid = "p";
    pa.assertedInferences.push_back(rel_a);

    sacm::ArgumentPackage pb;
    pb.id = "p";
    pb.gid = "p";
    pb.assertedInferences.push_back(rel_b);

    auto a = MakeCase({pa});
    auto b = MakeCase({pb});
    EXPECT_EQ(core::audit::CanonicalModelHash(a), core::audit::CanonicalModelHash(b));
}
