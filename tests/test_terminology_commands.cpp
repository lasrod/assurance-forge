#include "core/commands/terminology_commands.h"

#include "core/commands/command_bus.h"
#include "core/terminology_package_service.h"
#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <string>

// Intent of the terminology command classes (core/commands/terminology_commands.h):
// each command's Apply() performs the matching terminology mutation on the
// CommandContext's package, and — on success — fills the audit event with a
// PascalCase event_type plus a payload that captures the identities needed to
// replay the action deterministically. On failure it returns false, sets
// out_error, and leaves the model unchanged. Assertions encode that contract;
// failures are findings, not tuning targets.

namespace {

std::string PayloadString(const core::audit::AuditEvent& event, const char* key) {
    return event.payload.at(key).get<std::string>();
}

// Apply a CreateTerminologyPackageCommand and return its generated ref.
core::TerminologyPackageRef CreatePackage(core::commands::CommandContext& ctx, const std::string& name) {
    core::commands::CreateTerminologyPackageCommand command(name, "");
    core::audit::AuditEvent event;
    std::string error;
    EXPECT_TRUE(command.Apply(ctx, event, error)) << error;
    return command.GeneratedRef();
}

} // namespace

TEST(TerminologyCommandsTest, CreatePackageMutatesModelAndCapturesIdentityInPayload) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::commands::CommandContext ctx{model, package};

    core::commands::CreateTerminologyPackageCommand command("Project Terms", "Shared definitions.");
    core::audit::AuditEvent event;
    std::string error;

    ASSERT_TRUE(command.Apply(ctx, event, error)) << error;

    // The package now exists and matches the recorded generated identity.
    const core::TerminologyPackageRef& ref = command.GeneratedRef();
    EXPECT_FALSE(ref.id.empty());
    const sacm::TerminologyPackage* created = core::FindTerminologyPackage(package, ref);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->name, "Project Terms");

    EXPECT_EQ(event.event_type, "CreateTerminologyPackage");
    EXPECT_EQ(PayloadString(event, "name"), "Project Terms");
    EXPECT_EQ(PayloadString(event, "generated_id"), ref.id);
    EXPECT_EQ(PayloadString(event, "generated_gid"), ref.gid);
}

TEST(TerminologyCommandsTest, UpdatePackageEditsNameAndDescription) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::commands::CommandContext ctx{model, package};
    const core::TerminologyPackageRef ref = CreatePackage(ctx, "Old");

    core::commands::UpdateTerminologyPackageCommand command(ref, "New", "New description");
    core::audit::AuditEvent event;
    std::string error;
    ASSERT_TRUE(command.Apply(ctx, event, error)) << error;

    const sacm::TerminologyPackage* updated = core::FindTerminologyPackage(package, ref);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->name, "New");
    EXPECT_EQ(updated->description, "New description");
    EXPECT_EQ(event.event_type, "UpdateTerminologyPackage");
}

TEST(TerminologyCommandsTest, CreateAndDeleteCategoryRoundTrip) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::commands::CommandContext ctx{model, package};
    const core::TerminologyPackageRef package_ref = CreatePackage(ctx, "Terms");

    core::TerminologyCategoryDraft draft;
    draft.name = "Operational Context";
    core::commands::CreateTerminologyCategoryCommand create(package_ref, draft);
    core::audit::AuditEvent create_event;
    std::string error;
    ASSERT_TRUE(create.Apply(ctx, create_event, error)) << error;

    const core::TerminologyCategoryRef& category_ref = create.GeneratedRef();
    EXPECT_FALSE(category_ref.id.empty());
    ASSERT_NE(core::FindTerminologyCategory(package, package_ref, category_ref), nullptr);
    EXPECT_EQ(create_event.event_type, "CreateTerminologyCategory");
    EXPECT_EQ(PayloadString(create_event, "generated_id"), category_ref.id);

    core::commands::DeleteTerminologyCategoryCommand del(package_ref, category_ref);
    core::audit::AuditEvent delete_event;
    ASSERT_TRUE(del.Apply(ctx, delete_event, error)) << error;
    EXPECT_EQ(core::FindTerminologyCategory(package, package_ref, category_ref), nullptr);
    EXPECT_EQ(delete_event.event_type, "DeleteTerminologyCategory");
}

TEST(TerminologyCommandsTest, CreateUpdateAndDeleteTermRoundTrip) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::commands::CommandContext ctx{model, package};
    const core::TerminologyPackageRef package_ref = CreatePackage(ctx, "Terms");

    core::TerminologyTermDraft draft;
    draft.value = "hazard";
    draft.description = "A potential source of harm.";
    core::commands::CreateTerminologyTermCommand create(package_ref, draft);
    core::audit::AuditEvent create_event;
    std::string error;
    ASSERT_TRUE(create.Apply(ctx, create_event, error)) << error;

    const core::TerminologyTermRef& term_ref = create.GeneratedRef();
    EXPECT_FALSE(term_ref.id.empty());
    const sacm::Term* term = core::FindTerminologyTerm(package, package_ref, term_ref);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->value, "hazard");
    EXPECT_EQ(create_event.event_type, "CreateTerminologyTerm");
    EXPECT_EQ(PayloadString(create_event, "generated_id"), term_ref.id);

    core::TerminologyTermDraft update = draft;
    update.value = "risk";
    core::commands::UpdateTerminologyTermCommand update_command(package_ref, term_ref, update);
    core::audit::AuditEvent update_event;
    ASSERT_TRUE(update_command.Apply(ctx, update_event, error)) << error;
    term = core::FindTerminologyTerm(package, package_ref, term_ref);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->value, "risk");
    EXPECT_EQ(update_event.event_type, "UpdateTerminologyTerm");

    core::commands::DeleteTerminologyTermCommand del(package_ref, term_ref);
    core::audit::AuditEvent delete_event;
    ASSERT_TRUE(del.Apply(ctx, delete_event, error)) << error;
    EXPECT_EQ(core::FindTerminologyTerm(package, package_ref, term_ref), nullptr);
    EXPECT_EQ(delete_event.event_type, "DeleteTerminologyTerm");
}

TEST(TerminologyCommandsTest, AssociateTermWithElementPopulatesResult) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim claim;
    claim.id = "G1";
    claim.content = "The ODD is well defined.";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);
    core::commands::CommandContext ctx{model, package};

    const core::TerminologyPackageRef package_ref = CreatePackage(ctx, "Terms");
    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    core::commands::CreateTerminologyTermCommand create_term(package_ref, draft);
    core::audit::AuditEvent term_event;
    std::string error;
    ASSERT_TRUE(create_term.Apply(ctx, term_event, error)) << error;

    core::commands::AssociateTerminologyTermWithElementCommand associate("G1", package_ref, create_term.GeneratedRef());
    core::audit::AuditEvent event;
    ASSERT_TRUE(associate.Apply(ctx, event, error)) << error;

    EXPECT_TRUE(associate.Result().success);
    EXPECT_FALSE(associate.Result().artifact_reference_id.empty());
    EXPECT_EQ(event.event_type, "AssociateTerminologyTermWithElement");
}

TEST(TerminologyCommandsTest, AddVisibleContextSyncsParserProjection) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim claim;
    claim.id = "G1";
    claim.content = "The ODD is well defined.";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);
    // The parser model must already know G1 for the projection to attach to it.
    parser::SacmElement element;
    element.id = "G1";
    element.type = "claim";
    element.content = "The ODD is well defined.";
    model.elements.push_back(element);
    core::commands::CommandContext ctx{model, package};

    const core::TerminologyPackageRef package_ref = CreatePackage(ctx, "Terms");
    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    draft.description = "The operating conditions for the system.";
    core::commands::CreateTerminologyTermCommand create_term(package_ref, draft);
    core::audit::AuditEvent term_event;
    std::string error;
    ASSERT_TRUE(create_term.Apply(ctx, term_event, error)) << error;

    const std::size_t elements_before = model.elements.size();
    core::commands::AddTerminologyTermAsVisibleContextCommand command("G1", package_ref, create_term.GeneratedRef());
    core::audit::AuditEvent event;
    ASSERT_TRUE(command.Apply(ctx, event, error)) << error;

    EXPECT_TRUE(command.Result().success);
    EXPECT_EQ(event.event_type, "AddTerminologyTermAsVisibleContext");
    // A visible context projects new artifact-reference / context elements into
    // the parser model so replayed history sees the same projection.
    EXPECT_GT(model.elements.size(), elements_before);
}

TEST(TerminologyCommandsTest, DeleteMissingTermFailsWithoutMutatingModel) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::commands::CommandContext ctx{model, package};
    const core::TerminologyPackageRef package_ref = CreatePackage(ctx, "Terms");

    core::commands::DeleteTerminologyTermCommand command(package_ref, core::TerminologyTermRef{"NOPE", "gid-NOPE"});
    core::audit::AuditEvent event;
    std::string error;

    EXPECT_FALSE(command.Apply(ctx, event, error));
    EXPECT_FALSE(error.empty());
    // The terminology package must be untouched (still present, no terms added/removed).
    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, package_ref);
    ASSERT_NE(terms, nullptr);
    EXPECT_TRUE(terms->terms.empty());
}
