#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
SACM 2.3 editable library-first agent pack installer

Usage:
  bash scripts/sacm-agent-bootstrap.sh [options]

Options:
  --repo PATH             Target repository root. Defaults to current directory.
  --dry-run               Show actions without copying files.
  --force                 Overwrite changed destination files without backups.
  --no-backup             Overwrite changed destination files without backups.
  --scaffold-library      Create a minimal standalone SACM library skeleton after install.
  --library-path PATH     Library path for --scaffold-library. Default: libs/sacm.
  --verify-only           Only verify that expected files are installed in --repo.
  --help                  Show this help.
USAGE
}

SCRIPT_PATH="${BASH_SOURCE[0]}"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"
PACK_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_FILES="$PACK_ROOT/repo-files"
REPO_ROOT="$(pwd)"
DRY_RUN=0
FORCE=0
BACKUP=1
SCAFFOLD=0
VERIFY_ONLY=0
LIBRARY_PATH="libs/sacm"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repo)
      if [ "$#" -lt 2 ]; then echo "error: --repo needs a path" >&2; exit 2; fi
      REPO_ROOT="$2"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --force|--no-backup)
      FORCE=1
      BACKUP=0
      shift
      ;;
    --scaffold-library)
      SCAFFOLD=1
      shift
      ;;
    --library-path)
      if [ "$#" -lt 2 ]; then echo "error: --library-path needs a path" >&2; exit 2; fi
      LIBRARY_PATH="$2"
      shift 2
      ;;
    --verify-only)
      VERIFY_ONLY=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"

if [ ! -d "$REPO_ROOT" ]; then
  echo "error: repo path does not exist: $REPO_ROOT" >&2
  exit 1
fi

if [ ! -d "$REPO_FILES" ] && [ "$VERIFY_ONLY" -eq 0 ]; then
  echo "error: cannot find repo-files next to the installer: $REPO_FILES" >&2
  echo "Run this script from the extracted agent pack, or use --verify-only after installation." >&2
  exit 1
fi

warn_if_not_assurance_forge() {
  if [ ! -f "$REPO_ROOT/CMakeLists.txt" ] || [ ! -f "$REPO_ROOT/README.md" ]; then
    echo "warning: $REPO_ROOT does not look like a CMake repository root" >&2
  fi
  if [ ! -d "$REPO_ROOT/.git" ]; then
    echo "warning: $REPO_ROOT has no .git directory; continuing anyway" >&2
  fi
}

copy_file() {
  local src="$1"
  local rel="$2"
  local dst="$REPO_ROOT/$rel"
  local dst_dir
  dst_dir="$(dirname "$dst")"

  if [ "$DRY_RUN" -eq 1 ]; then
    if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
      echo "unchanged $rel"
    elif [ -f "$dst" ]; then
      echo "would update $rel"
    else
      echo "would create $rel"
    fi
    return
  fi

  mkdir -p "$dst_dir"

  if [ -f "$dst" ]; then
    if cmp -s "$src" "$dst"; then
      echo "unchanged $rel"
      return
    fi
    if [ "$FORCE" -eq 0 ] && [ "$BACKUP" -eq 1 ]; then
      local stamp
      stamp="$(date +%Y%m%d%H%M%S)"
      cp "$dst" "$dst.bak.$stamp"
      echo "backup $rel -> $rel.bak.$stamp"
    fi
    echo "update $rel"
  else
    echo "create $rel"
  fi
  cp "$src" "$dst"
}

verify_file() {
  local rel="$1"
  if [ -f "$REPO_ROOT/$rel" ]; then
    echo "ok $rel"
  else
    echo "missing $rel" >&2
    return 1
  fi
}

verify_installation() {
  local failures=0
  verify_file ".claude/agents/sacm-implementation-lead.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-library-architect.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-spec-analyst.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-metamodel-cartographer.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-xmi-test-engineer.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-library-implementer.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-conformance-verifier.md" || failures=$((failures + 1))
  verify_file ".claude/agents/assurance-forge-adapter-engineer.md" || failures=$((failures + 1))
  verify_file ".claude/agents/sacm-interop-researcher.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-library-implementation-plan.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-library-architecture.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-editing-policy.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-layout-policy.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-decisions-and-questions.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-conformance-matrix.md" || failures=$((failures + 1))
  verify_file "docs/sacm/sacm-test-strategy.md" || failures=$((failures + 1))
  verify_file "docs/sacm/prompts/sacm-library-master-prompt.md" || failures=$((failures + 1))
  verify_file "docs/sacm/prompts/sacm-first-editable-slice-prompt.md" || failures=$((failures + 1))
  verify_file "docs/sacm/prompts/sacm-verification-prompt.md" || failures=$((failures + 1))
  verify_file "scripts/scaffold-sacm-library.sh" || failures=$((failures + 1))
  verify_file "scripts/fetch-sacm23-references.sh" || failures=$((failures + 1))
  return "$failures"
}

warn_if_not_assurance_forge

if [ "$VERIFY_ONLY" -eq 1 ]; then
  verify_installation
  exit $?
fi

while IFS= read -r -d '' src; do
  rel="${src#$REPO_FILES/}"
  copy_file "$src" "$rel"
done < <(find "$REPO_FILES" -type f -print0 | sort -z)

if [ "$DRY_RUN" -eq 0 ]; then
  chmod +x "$REPO_ROOT/scripts/sacm-agent-bootstrap.sh" || true
  chmod +x "$REPO_ROOT/scripts/fetch-sacm23-references.sh" || true
  chmod +x "$REPO_ROOT/scripts/scaffold-sacm-library.sh" || true
fi

if [ "$SCAFFOLD" -eq 1 ]; then
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "would scaffold library at $LIBRARY_PATH"
  else
    bash "$REPO_ROOT/scripts/scaffold-sacm-library.sh" --repo "$REPO_ROOT" --path "$LIBRARY_PATH"
  fi
fi

if [ "$DRY_RUN" -eq 1 ]; then
  echo "dry run complete for SACM editable library-first agent pack into $REPO_ROOT"
else
  echo "installed SACM editable library-first agent pack into $REPO_ROOT"
fi

echo "next: open Claude Code at the repo root and use docs/sacm/prompts/sacm-library-master-prompt.md"
echo "first implementation prompt: docs/sacm/prompts/sacm-first-editable-slice-prompt.md"
