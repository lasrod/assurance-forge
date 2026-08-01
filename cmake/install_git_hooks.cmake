# Point git at the repository's checked-in hooks.
#
# Hooks in `.git/hooks` are not version controlled, so a hook committed to the
# repository only reaches anyone through `core.hooksPath`. Configure time is the
# right moment to set it: every contributor builds, and nobody has to remember a
# separate setup step.
#
# This writes to the developer's LOCAL repository config only -- never global,
# never the working tree.

if(NOT EXISTS "${AF_REPO_ROOT}/.githooks")
    return()
endif()

find_package(Git QUIET)
if(NOT GIT_FOUND)
    message(STATUS "Git hooks: git not found; skipping hook installation.")
    return()
endif()

# A bare `.git` file rather than a directory means a worktree or submodule
# checkout; `core.hooksPath` still applies, but there is no repository to
# configure when this source tree is not a checkout at all (a release tarball).
if(NOT EXISTS "${AF_REPO_ROOT}/.git")
    message(STATUS "Git hooks: not a git checkout; skipping hook installation.")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" config --local --get core.hooksPath
    WORKING_DIRECTORY "${AF_REPO_ROOT}"
    OUTPUT_VARIABLE existing_hooks_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(existing_hooks_path STREQUAL ".githooks")
    return()
endif()

# Someone who deliberately points git at their own hooks keeps them. Silently
# overwriting that would be taking over a developer's tooling to enforce a
# formatting convention, which is out of proportion.
if(NOT existing_hooks_path STREQUAL "")
    message(STATUS
        "Git hooks: core.hooksPath is already set to '${existing_hooks_path}'; leaving it alone. "
        "Run `git config --local core.hooksPath .githooks` to use the project's hooks.")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" config --local core.hooksPath .githooks
    WORKING_DIRECTORY "${AF_REPO_ROOT}"
    RESULT_VARIABLE config_result
    ERROR_QUIET
)

if(config_result EQUAL 0)
    message(STATUS "Git hooks: core.hooksPath set to .githooks (staged C/C++ is formatted on commit).")
else()
    message(STATUS "Git hooks: could not set core.hooksPath; commits will not be formatted automatically.")
endif()
