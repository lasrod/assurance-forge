import io
import os
import pathlib
import sys
import unittest
from unittest import mock

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

import roadmap_create_epic  # noqa: E402


class FakeGitHubClient:
    def __init__(self, repo, token):
        self.repo = repo
        self.token = token

    def get_issue(self, issue_number):
        return {
            "number": issue_number,
            "body": "<!-- roadmap-automation: generated=true af_id=AF-E-0004 -->",
            "labels": [{"name": "roadmap-generated"}],
        }

    def get_actor_permission(self, actor):
        return "admin"


class RoadmapCreateEpicTests(unittest.TestCase):
    def test_reapproval_of_generated_epic_retries_followup_even_without_failed_label(self):
        environment = {
            "GH_TOKEN": "token",
            "REPO": "owner/repo",
            "ISSUE_NUMBER": "84",
            "ACTOR": "maintainer",
            "EVENT_LABEL": "roadmap-approved",
            "PROJECT_OWNER": "owner",
            "PROJECT_NUMBER": "2",
        }

        with mock.patch.dict(os.environ, environment, clear=True), \
             mock.patch.object(roadmap_create_epic, "GitHubClient", FakeGitHubClient), \
             mock.patch.object(roadmap_create_epic, "ensure_roadmap_labels"), \
               mock.patch.object(roadmap_create_epic, "retry_generated_followup", return_value=0) as retry, \
               mock.patch("sys.stdout", new_callable=io.StringIO):
            self.assertEqual(0, roadmap_create_epic.main())

        retry.assert_called_once()
        _, issue, issue_number, _, project_owner, project_number = retry.call_args.args
        self.assertEqual(84, issue_number)
        self.assertEqual("owner", project_owner)
        self.assertEqual("2", project_number)
        self.assertIn("roadmap-automation: generated=true", issue["body"])


if __name__ == "__main__":
    unittest.main()
