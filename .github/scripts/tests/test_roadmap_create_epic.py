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
    issue_labels = [{"name": "roadmap-generated"}]
    comments = []

    def __init__(self, repo, token):
        self.repo = repo
        self.token = token

    def get_issue(self, issue_number):
        return {
            "number": issue_number,
            "body": "<!-- roadmap-automation: generated=true af_id=AF-E-0004 -->",
            "labels": self.issue_labels,
        }

    def get_actor_permission(self, actor):
        return "admin"

    def create_comment(self, issue_number, body):
        self.comments.append((issue_number, body))


class RoadmapCreateEpicTests(unittest.TestCase):
    def setUp(self):
        FakeGitHubClient.issue_labels = [{"name": "roadmap-generated"}]
        FakeGitHubClient.comments = []

    def test_reapproval_of_successful_generated_epic_does_not_retry_followup(self):
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

        retry.assert_not_called()
        self.assertEqual(1, len(FakeGitHubClient.comments))
        self.assertIn("already generated", FakeGitHubClient.comments[0][1])

    def test_reapproval_of_failed_generated_epic_retries_followup(self):
        FakeGitHubClient.issue_labels = [{"name": "roadmap-generated"}, {"name": "roadmap-failed"}]
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

    def test_roadmap_retry_label_retries_generated_epic_followup(self):
        FakeGitHubClient.issue_labels = [{"name": "roadmap-generated"}, {"name": "roadmap-retry"}]
        environment = {
            "GH_TOKEN": "token",
            "REPO": "owner/repo",
            "ISSUE_NUMBER": "84",
            "ACTOR": "maintainer",
            "EVENT_LABEL": "roadmap-retry",
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


if __name__ == "__main__":
    unittest.main()
