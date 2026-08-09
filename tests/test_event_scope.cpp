#include "core/audit/audit_transaction.h"
#include "core/audit/event_scope.h"
#include "legacy_sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

core::audit::AuditEvent MakeEvent(const char* type, nlohmann::ordered_json payload) {
    core::audit::AuditEvent e;
    e.event_type = type;
    e.payload = std::move(payload);
    return e;
}

core::audit::AuditTransaction MakeTx(std::uint64_t seq, std::vector<core::audit::AuditEvent> events) {
    core::audit::AuditTransaction tx;
    tx.transaction_sequence = seq;
    tx.events = std::move(events);
    return tx;
}

sacm::ArgumentPackage MakePackageWithClaim(const std::string& claim_id, const std::string& claim_gid) {
    sacm::ArgumentPackage pkg;
    pkg.id = "AP1";
    pkg.gid = "AP1.gid";
    sacm::Claim c;
    c.id = claim_id;
    c.gid = claim_gid;
    pkg.claims.push_back(std::move(c));
    return pkg;
}

} // namespace

TEST(EventScope, CollectArgumentPackageScopeIncludesAllElementKinds) {
    sacm::ArgumentPackage pkg;
    sacm::Claim claim;
    claim.id = "G1";
    pkg.claims.push_back(claim);
    sacm::ArgumentReasoning reasoning;
    reasoning.id = "S1";
    pkg.argumentReasonings.push_back(reasoning);
    sacm::ArtifactReference art;
    art.id = "Sn1";
    pkg.artifactReferences.push_back(art);
    sacm::AssertedInference inf;
    inf.id = "AI1";
    pkg.assertedInferences.push_back(inf);
    sacm::AssertedContext ctx;
    ctx.id = "AC1";
    pkg.assertedContexts.push_back(ctx);
    sacm::AssertedEvidence ev;
    ev.id = "AE1";
    pkg.assertedEvidences.push_back(ev);

    const auto scope = core::audit::CollectArgumentPackageScope(pkg);
    EXPECT_EQ(scope.element_ids.count("G1"), 1u);
    EXPECT_EQ(scope.element_ids.count("S1"), 1u);
    EXPECT_EQ(scope.element_ids.count("Sn1"), 1u);
    EXPECT_EQ(scope.element_ids.count("AI1"), 1u);
    EXPECT_EQ(scope.element_ids.count("AC1"), 1u);
    EXPECT_EQ(scope.element_ids.count("AE1"), 1u);
}

TEST(EventScope, CollectEventElementIdsReadsKnownPayloadFields) {
    auto event = MakeEvent("CreateChildElement",
                           {{"parent_id", "G1"}, {"generated_id", "S1"}, {"generated_relationship_id", "AI1"}});
    const auto ids = core::audit::CollectEventElementIds(event);
    EXPECT_EQ(ids.size(), 3u);
}

TEST(EventScope, CollectEventElementIdsReadsDeletedIdsArray) {
    auto event = MakeEvent(
        "RemoveElement", {{"element_id", "S1"}, {"deleted_ids", nlohmann::ordered_json::array({"S1", "AI1", "Sn1"})}});
    const auto ids = core::audit::CollectEventElementIds(event);
    // element_id + 3 deleted_ids = 4
    EXPECT_EQ(ids.size(), 4u);
}

TEST(EventScope, TransactionTouchesScopeMatchesById) {
    const auto pkg = MakePackageWithClaim("G1", "G1.gid");
    const auto scope = core::audit::CollectArgumentPackageScope(pkg);
    auto tx = MakeTx(1,
                     {MakeEvent("CreateChildElement",
                                {{"parent_id", "G1"}, {"generated_id", "S1"}, {"generated_relationship_id", "AI1"}})});
    EXPECT_TRUE(core::audit::TransactionTouchesScope(tx, scope));
}

TEST(EventScope, TransactionTouchesScopeMatchesByGid) {
    const auto pkg = MakePackageWithClaim("G1", "G1.gid");
    const auto scope = core::audit::CollectArgumentPackageScope(pkg);
    auto tx =
        MakeTx(1,
               {MakeEvent("RemoveElement",
                          {{"element_id", "G1.gid"}, {"deleted_ids", nlohmann::ordered_json::array({"G1.gid"})}})});
    EXPECT_TRUE(core::audit::TransactionTouchesScope(tx, scope));
}

TEST(EventScope, TransactionTouchesScopeRejectsForeignIds) {
    const auto pkg = MakePackageWithClaim("G1", "G1.gid");
    const auto scope = core::audit::CollectArgumentPackageScope(pkg);
    auto tx = MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G99"}})});
    EXPECT_FALSE(core::audit::TransactionTouchesScope(tx, scope));
}

TEST(EventScope, TransactionTouchesScopeIgnoresMalformedPayload) {
    const auto pkg = MakePackageWithClaim("G1", "G1.gid");
    const auto scope = core::audit::CollectArgumentPackageScope(pkg);
    core::audit::AuditEvent event;
    event.event_type = "Custom";
    event.payload = nlohmann::ordered_json::array(); // not an object
    auto tx = MakeTx(1, {event});
    EXPECT_FALSE(core::audit::TransactionTouchesScope(tx, scope));
}
