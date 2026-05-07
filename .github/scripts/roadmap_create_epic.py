from __future__ import annotations

import os
import sys

from roadmap_common import (
    GitHubApiError,
    GitHubClient,
    add_project_item,
    apply_issue_data_to_task,
    area_label,
    build_request_from_issue,
    build_request_from_generated_epic,
    ensure_roadmap_labels,
    existing_tasks_by_id,
    generate_tasks,
    generated_tasks_from_epic_body,
    has_generated_marker,
    is_trusted_actor_permission,
    issue_labels,
    project_fields,
    project_values_for_request,
    render_epic_body,
    render_task_body,
    resolve_project_id,
    set_project_fields,
    validate_request,
)


def print_failure(message: str) -> None:
    print(message, file=sys.stderr)


def print_warnings(prefix: str, warnings: list[str]) -> None:
    print_failure(prefix)
    for warning in warnings:
        print_failure(f"- {warning}")


def append_unique_warnings(warnings: list[str], new_warnings: list[str]) -> None:
    for warning in new_warnings:
        if warning not in warnings:
            warnings.append(warning)


def fail_for_untrusted_actor(client: GitHubClient, issue_number: int, actor: str, permission: str) -> int:
    message = (
        "Roadmap generation did not run because `roadmap-approved` was applied by "
        f"`{actor}` with repository permission `{permission or 'none'}`. "
        "A repository owner, maintainer, or collaborator with write access must approve roadmap generation."
    )
    print(message)
    client.add_labels(issue_number, ["roadmap-failed"])
    client.remove_label(issue_number, "roadmap-approved")
    client.create_comment(issue_number, message)
    return 1


def update_project_items(client: GitHubClient, token: str, owner: str, number: str, request,
                         epic_issue: dict, task_issues: list) -> list[str]:
    warnings: list[str] = []
    if not number:
        return ["ROADMAP_PROJECT_NUMBER is not configured, so Project fields were not updated."]
    try:
        project_id = resolve_project_id(client, owner, number, token)
        fields = project_fields(client, project_id, token)
        epic_item_id = add_project_item(client, project_id, epic_issue["node_id"], token)
        append_unique_warnings(
            warnings,
            set_project_fields(
                client,
                project_id,
                epic_item_id,
                fields,
                project_values_for_request(request, request.af_id, include_points=True),
                token,
            ),
        )
        for task in task_issues:
            if not task.node_id:
                continue
            item_id = add_project_item(client, project_id, task.node_id, token)
            append_unique_warnings(
                warnings,
                set_project_fields(
                    client,
                    project_id,
                    item_id,
                    fields,
                    project_values_for_request(request, task.task_id, include_points=False),
                    token,
                ),
            )
    except (GitHubApiError, ValueError) as error:
        warnings.append(f"Project update failed: {error}")
    return warnings


def link_missing_sub_issues(client: GitHubClient, issue_number: int, tasks: list) -> list[str]:
    warnings: list[str] = []
    existing_sub_issue_ids = {sub_issue.get("id") for sub_issue in client.list_sub_issues(issue_number)}
    for task in tasks:
        if task.issue_id is None or task.issue_id in existing_sub_issue_ids:
            continue
        try:
            client.add_sub_issue(issue_number, task.issue_id)
        except GitHubApiError as error:
            warnings.append(f"Sub-issue linking failed for {task.task_id}: {error}")
    return warnings


def retry_generated_followup(client: GitHubClient, issue: dict, issue_number: int, roadmap_token: str,
                             project_owner: str, project_number: str) -> int:
    request = build_request_from_generated_epic(issue)
    tasks = generated_tasks_from_epic_body(issue.get("body") or "")
    existing_by_id = existing_tasks_by_id(client.list_issues(labels=["roadmap-generated"]), tasks)
    resolved_tasks = []
    for task in tasks:
        existing_issue = existing_by_id.get(task.task_id)
        if existing_issue:
            resolved_tasks.append(apply_issue_data_to_task(task, existing_issue))
        else:
            resolved_tasks.append(task)

    warnings = link_missing_sub_issues(client, issue_number, resolved_tasks)
    warnings.extend(update_project_items(client, roadmap_token, project_owner, project_number, request, issue, resolved_tasks))
    if warnings:
        print_warnings("Roadmap follow-up retry failed:", warnings)
        client.add_labels(issue_number, ["roadmap-failed"])
        client.remove_label(issue_number, "roadmap-approved")
        warning_text = "\n".join(f"- {warning}" for warning in warnings)
        client.create_comment(
            issue_number,
            "Roadmap automation found an existing generated epic and retried follow-up work, but some steps still failed:\n\n"
            + warning_text,
        )
        return 1

    client.remove_label(issue_number, "roadmap-failed")
    client.create_comment(
        issue_number,
        "Roadmap automation found an existing generated epic. No duplicate tasks were created, and follow-up Project/sub-issue work completed successfully.",
    )
    return 0


def main() -> int:
    token = os.environ.get("GH_TOKEN", "")
    roadmap_token = os.environ.get("ROADMAP_TOKEN") or token
    repo = os.environ.get("REPO", "")
    issue_number = int(os.environ.get("ISSUE_NUMBER", "0"))
    actor = os.environ.get("ACTOR", "")
    event_label = os.environ.get("EVENT_LABEL", "")
    project_owner = os.environ.get("PROJECT_OWNER", "lasrod")
    project_number = os.environ.get("PROJECT_NUMBER", "")

    if event_label != "roadmap-approved":
        print("Event label was not roadmap-approved; no action taken.")
        return 0
    if not token or not repo or not issue_number:
        print("GH_TOKEN, REPO, and ISSUE_NUMBER are required", file=sys.stderr)
        return 2

    client = GitHubClient(repo, token)
    issue = client.get_issue(issue_number)
    ensure_roadmap_labels(client)

    actor_permission = client.get_actor_permission(actor)
    if not is_trusted_actor_permission(actor_permission):
        return fail_for_untrusted_actor(client, issue_number, actor, actor_permission)
    print(f"Roadmap approval actor `{actor}` has repository permission `{actor_permission}`.")

    if has_generated_marker(issue.get("body") or ""):
        return retry_generated_followup(client, issue, issue_number, roadmap_token, project_owner, project_number)

    request = build_request_from_issue(issue)
    validation = validate_request(request, require_af_id=True)
    if not validation.ok:
        client.add_labels(issue_number, ["roadmap-failed", "needs-roadmap-review"])
        client.remove_label(issue_number, "roadmap-approved")
        errors = "\n".join(f"- {error}" for error in validation.errors)
        print_failure("Roadmap generation failed validation:\n" + errors)
        client.create_comment(issue_number, f"Roadmap generation failed validation:\n\n{errors}")
        return 1

    tasks = generate_tasks(request.af_id, request.tasks)
    existing_issues = client.list_issues(labels=["roadmap-generated"])
    existing_by_id = existing_tasks_by_id(existing_issues, tasks)

    task_labels = ["type: task", "roadmap-generated", area_label(request.area)]
    resolved_tasks = []
    for task in tasks:
        existing_issue = existing_by_id.get(task.task_id)
        if existing_issue:
            resolved_tasks.append(apply_issue_data_to_task(task, existing_issue))
            continue
        created = client.create_issue(
            f"[{task.task_id}] {task.title}",
            render_task_body(request, task, issue_number),
            task_labels,
        )
        resolved_tasks.append(apply_issue_data_to_task(task, created))

    warnings = link_missing_sub_issues(client, issue_number, resolved_tasks)

    epic_title = f"[{request.af_id}] {request.title}"
    epic_labels = sorted(set(issue_labels(issue) + ["type: epic", "roadmap-generated", area_label(request.area)]))
    epic_issue = client.update_issue(issue_number, title=epic_title, body=render_epic_body(request, resolved_tasks), labels=epic_labels)
    client.remove_label(issue_number, "needs-roadmap-review")

    warnings.extend(update_project_items(client, roadmap_token, project_owner, project_number, request, epic_issue, resolved_tasks))
    if warnings:
        print_warnings("Roadmap follow-up automation failed:", warnings)
        client.add_labels(issue_number, ["roadmap-failed"])
        client.remove_label(issue_number, "roadmap-approved")
        warning_text = "\n".join(f"- {warning}" for warning in warnings)
        client.create_comment(
            issue_number,
            "Roadmap epic structure was generated, but some follow-up automation failed:\n\n" + warning_text,
        )
        return 1

    client.remove_label(issue_number, "roadmap-failed")
    generated_lines = "\n".join(f"- #{task.issue_number} [{task.task_id}] {task.title}" for task in resolved_tasks)
    client.create_comment(
        issue_number,
        "Roadmap epic structure generated successfully.\n\nGenerated subtasks:\n\n" + generated_lines,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())