import pathlib
import sys
import unittest

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

from roadmap_common import (  # noqa: E402
    GeneratedTask,
    GitHubApiError,
    RoadmapRequest,
    area_label,
    build_request_from_issue,
    build_request_from_generated_epic,
    completed_points,
    generate_tasks,
    has_generated_marker,
    is_trusted_actor_permission,
    parse_issue_form,
    render_epic_body,
    resolve_project_id,
    set_project_fields,
    validate_request,
)


ISSUE_BODY = """### AF Epic ID
AF-E-0004

### Related idea ID
AF-I-0004

### Related discussion URL
https://github.com/lasrod/assurance-forge/discussions/123

### Area
Storage

### Initial maturity
Prototype 1

### Size Points
13

### Priority
Critical

### Target Release
v0.2

### Public roadmap bucket
Now

### Summary
Add an audit trail.

### Epic acceptance criteria
- Events are persisted.
- Tests exist.

### Suggested subtasks
Define audit event schema
Add audit history UI
"""


def request(**overrides):
    values = {
        "af_id": "AF-E-0004",
        "idea_id": "AF-I-0004",
        "discussion_url": "https://github.com/lasrod/assurance-forge/discussions/123",
        "area": "Storage",
        "maturity": "Prototype 1",
        "size_points": "13",
        "priority": "Critical",
        "target_release": "v0.2",
        "public_roadmap": "Now",
        "summary": "Add an audit trail.",
        "acceptance_criteria": "- Events are persisted.",
        "tasks": ["Define audit event schema", "Add audit history UI"],
        "title": "History / Audit Trail",
    }
    values.update(overrides)
    return RoadmapRequest(**values)


class FakeGraphQLClient:
    def __init__(self, responses):
        self.responses = list(responses)
        self.queries = []

    def graphql(self, query, variables, token=None):
        self.queries.append(query)
        response = self.responses.pop(0)
        if isinstance(response, Exception):
            raise response
        return response


class RoadmapCommonTests(unittest.TestCase):
    def test_parse_issue_form(self):
        fields = parse_issue_form(ISSUE_BODY.replace("\n", "\r\n"))

        self.assertEqual("AF-E-0004", fields["af_id"])
        self.assertEqual("Prototype 1", fields["maturity"])
        self.assertIn("Define audit event schema", fields["tasks_text"])

    def test_build_request_from_issue_extracts_title_and_tasks(self):
        parsed = build_request_from_issue({"title": "[EPIC REQUEST]: History / Audit Trail", "body": ISSUE_BODY})

        self.assertEqual("History / Audit Trail", parsed.title)
        self.assertEqual(["Define audit event schema", "Add audit history UI"], parsed.tasks)

    def test_request_time_af_id_is_optional(self):
        result = validate_request(request(af_id=""), require_af_id=False)

        self.assertTrue(result.ok)

    def test_approval_time_af_id_is_required(self):
        result = validate_request(request(af_id=""), require_af_id=True)

        self.assertFalse(result.ok)
        self.assertIn("AF Epic ID is required", result.errors[0])

    def test_rejects_duplicate_tasks(self):
        result = validate_request(request(tasks=["Same", "same"]), require_af_id=True)

        self.assertFalse(result.ok)
        self.assertTrue(any("Duplicate task title" in error for error in result.errors))

    def test_rejects_too_many_tasks(self):
        result = validate_request(request(tasks=[f"Task {index}" for index in range(21)]), require_af_id=True)

        self.assertFalse(result.ok)
        self.assertTrue(any("No more than 20" in error for error in result.errors))

    def test_task_id_generation(self):
        tasks = generate_tasks("AF-E-0004", ["One", "Two"])

        self.assertEqual(["AF-T-0004.1", "AF-T-0004.2"], [task.task_id for task in tasks])

    def test_completed_points_mapping(self):
        self.assertEqual(3.9, completed_points("13", "Prototype 1"))
        self.assertEqual(9.1, completed_points("13", "Prototype 2"))
        self.assertEqual(13.0, completed_points("13", "Ready"))
        self.assertEqual(0.0, completed_points("13", "Planned"))

    def test_generated_marker_detection(self):
        self.assertTrue(has_generated_marker("<!-- roadmap-automation: generated=true af_id=AF-E-0004 -->"))
        self.assertFalse(has_generated_marker("plain text"))

    def test_trusted_actor_permissions(self):
        self.assertTrue(is_trusted_actor_permission("admin"))
        self.assertTrue(is_trusted_actor_permission("MAINTAIN"))
        self.assertTrue(is_trusted_actor_permission("write"))
        self.assertFalse(is_trusted_actor_permission("triage"))
        self.assertFalse(is_trusted_actor_permission("read"))

    def test_resolve_project_id_uses_user_project_without_org_lookup(self):
        client = FakeGraphQLClient([{"user": {"projectV2": {"id": "PVT_user"}}}])

        self.assertEqual("PVT_user", resolve_project_id(client, "lasrod", "2", "token"))
        self.assertEqual(1, len(client.queries))
        self.assertIn("user(login: $owner)", client.queries[0])

    def test_resolve_project_id_falls_back_to_org_project(self):
        client = FakeGraphQLClient([
            GitHubApiError(400, "Could not resolve to a User with the login of 'example-org'."),
            {"organization": {"projectV2": {"id": "PVT_org"}}},
        ])

        self.assertEqual("PVT_org", resolve_project_id(client, "example-org", "2", "token"))
        self.assertEqual(2, len(client.queries))
        self.assertIn("organization(login: $owner)", client.queries[1])

    def test_resolve_project_id_error_explains_project_variables(self):
        client = FakeGraphQLClient([
            GitHubApiError(400, "Could not resolve to a ProjectV2 with the number 2."),
            GitHubApiError(400, "Could not resolve to an Organization with the login of 'lasrod'."),
        ])

        with self.assertRaises(GitHubApiError) as context:
            resolve_project_id(client, "lasrod", "2", "token")

        self.assertIn("ROADMAP_PROJECT_OWNER", context.exception.message)
        self.assertIn("ROADMAP_PROJECT_NUMBER", context.exception.message)
        self.assertIn("github.com/users/OWNER/projects/NUMBER", context.exception.message)
        self.assertIn("ROADMAP_TOKEN", context.exception.message)

    def test_set_project_fields_warns_about_missing_fields(self):
        client = FakeGraphQLClient([
            {"updateProjectV2ItemFieldValue": {"projectV2Item": {"id": "item"}}},
            {"updateProjectV2ItemFieldValue": {"projectV2Item": {"id": "item"}}},
        ])
        fields = {
            "AF ID": {"id": "field_af", "name": "AF ID", "__typename": "ProjectV2Field", "dataType": "TEXT"},
            "Automation Status": {
                "id": "field_status",
                "name": "Automation Status",
                "__typename": "ProjectV2SingleSelectField",
                "options": [{"id": "generated", "name": "Generated"}],
            },
        }

        warnings = set_project_fields(
            client,
            "project",
            "item",
            fields,
            {"AF ID": "AF-E-0004", "Discussion URL": "https://example.com", "Automation Status": "Generated"},
            "token",
        )

        self.assertEqual(2, len(client.queries))
        self.assertEqual(1, len(warnings))
        self.assertIn("Project field `Discussion URL` is missing", warnings[0])

    def test_set_project_fields_warns_about_missing_single_select_option(self):
        client = FakeGraphQLClient([])
        fields = {
            "Automation Status": {
                "id": "field_status",
                "name": "Automation Status",
                "__typename": "ProjectV2SingleSelectField",
                "options": [],
            },
        }

        warnings = set_project_fields(
            client,
            "project",
            "item",
            fields,
            {"Automation Status": "Generated"},
            "token",
        )

        self.assertEqual([], client.queries)
        self.assertEqual(
            ["Project field `Automation Status` has no option named `Generated`. Add that option to the single-select field."],
            warnings,
        )

    def test_set_project_fields_warns_when_text_or_number_field_is_single_select(self):
        client = FakeGraphQLClient([])
        fields = {
            "Discussion URL": {
                "id": "field_discussion",
                "name": "Discussion URL",
                "__typename": "ProjectV2SingleSelectField",
                "options": [{"id": "discussion", "name": "https://example.com"}],
            },
            "Size Points": {
                "id": "field_size",
                "name": "Size Points",
                "__typename": "ProjectV2SingleSelectField",
                "options": [{"id": "size", "name": "1.0"}],
            },
        }

        warnings = set_project_fields(
            client,
            "project",
            "item",
            fields,
            {"Discussion URL": "https://example.com", "Size Points": 1.0},
            "token",
        )

        self.assertEqual([], client.queries)
        self.assertEqual(
            [
                "Project field `Discussion URL` should be a Text field. Current field type is Single select.",
                "Project field `Size Points` should be a Number field. Current field type is Single select.",
            ],
            warnings,
        )

    def test_set_project_fields_allows_target_release_single_select(self):
        client = FakeGraphQLClient([
            {"updateProjectV2ItemFieldValue": {"projectV2Item": {"id": "item"}}},
        ])
        fields = {
            "Target Release": {
                "id": "field_release",
                "name": "Target Release",
                "__typename": "ProjectV2SingleSelectField",
                "options": [{"id": "v02", "name": "v0.2"}],
            },
        }

        warnings = set_project_fields(
            client,
            "project",
            "item",
            fields,
            {"Target Release": "v0.2"},
            "token",
        )

        self.assertEqual([], warnings)
        self.assertEqual(1, len(client.queries))

    def test_area_label_mapping(self):
        self.assertEqual("area: build-ci", area_label("Build / CI"))
        self.assertEqual("area: other", area_label("Other"))

    def test_render_epic_body_contains_marker_and_task_issue(self):
        body = render_epic_body(
            request(),
            [GeneratedTask("AF-T-0004.1", "Define audit event schema", issue_number=52)],
        )

        self.assertIn("roadmap-automation: generated=true af_id=AF-E-0004", body)
        self.assertIn("AF-T-0004.1 Define audit event schema #52", body)

    def test_build_request_from_generated_epic_body(self):
        body = render_epic_body(
            request(),
            [GeneratedTask("AF-T-0004.1", "Define audit event schema", issue_number=52)],
        )
        parsed = build_request_from_generated_epic({"title": "[AF-E-0004] History / Audit Trail", "body": body})

        self.assertEqual("AF-E-0004", parsed.af_id)
        self.assertEqual("History / Audit Trail", parsed.title)
        self.assertEqual(["Define audit event schema"], parsed.tasks)


if __name__ == "__main__":
    unittest.main()