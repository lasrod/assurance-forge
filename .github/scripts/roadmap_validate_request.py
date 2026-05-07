from __future__ import annotations

import os
import sys

from roadmap_common import (
    GitHubClient,
    build_request_from_issue,
    ensure_roadmap_labels,
    is_roadmap_request_issue,
    validate_request,
    validation_comment,
)


def main() -> int:
    token = os.environ.get("GH_TOKEN", "")
    repo = os.environ.get("REPO", "")
    issue_number = int(os.environ.get("ISSUE_NUMBER", "0"))
    if not token or not repo or not issue_number:
        print("GH_TOKEN, REPO, and ISSUE_NUMBER are required", file=sys.stderr)
        return 2

    client = GitHubClient(repo, token)
    issue = client.get_issue(issue_number)
    if not is_roadmap_request_issue(issue):
        print("Issue is not a roadmap request; no action taken.")
        return 0

    ensure_roadmap_labels(client)
    request = build_request_from_issue(issue)
    result = validate_request(request, require_af_id=False)
    client.add_labels(issue_number, ["roadmap-request", "needs-roadmap-review"])
    client.create_comment(issue_number, validation_comment(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())