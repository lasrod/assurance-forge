# User Guide

Task-oriented guidance for people **using** Assurance Forge. It describes the
application as it behaves today, including the parts that are not finished — a
guide that quietly skips an unimplemented button costs a reader more time than
one that names it.

> **Authority: reference.** This guide describes what exists. What the tool is
> *claimed* to do, per capability and with the tests behind each claim, is the
> [capability matrix](../features/feature-matrix.md); on conflict the matrix
> wins. Standards conformance is claimed separately in the
> [SACM](../sacm/sacm-conformance-matrix.md) and
> [GSN](../gsn/gsn-v3-conformance-matrix.md) matrices.

![The Assurance Forge window: case explorer, GSN canvas and element inspector](../screenshot/dark.png)

## Before you start

Assurance Forge is **alpha software**. SACM XML is the source of truth and the
tool is built not to lose it, but keep your assurance data in version control
and keep backups. See
[Status and limitations](https://github.com/lasrod/assurance-forge#status-and-limitations).

There are no pre-built binaries outside Windows x64, and `main` is usually ahead
of the latest release — see the
[build instructions](https://github.com/lasrod/assurance-forge#build-instructions).

## The tasks

| Page | Covers |
|---|---|
| [Open a project](open-a-project.md) | The welcome screen, creating and opening projects, what a project holds |
| [Navigate the argument](navigate-the-argument.md) | Case explorer, argument navigator, GSN canvas, panning, zoom, selection |
| [Edit the argument](edit-the-argument.md) | Adding and removing elements, the inspector, undeveloped goals, two languages |
| [Review an element](review-an-element.md) | Problems, manual review, SCCG-guided AI review, the history timeline |
| [Evidence and registers](evidence-and-registers.md) | The evidence register, the CSE register, recording and opening evidence |
| [Export a diagram](export-a-diagram.md) | SVG export, what it carries, what it discloses |
| [Connect an AI client](connect-an-ai-client.md) | The MCP server, consent, and working drafts |

## The example projects

Every page here can be followed against the bundled
[`kitchen-blender`](https://github.com/lasrod/assurance-forge-examples/tree/main/projects/kitchen-blender)
example, which is the case shown in the screenshots. Initialize the submodule
and open `examples/projects/kitchen-blender/af.proj`:

```bash
git submodule update --init examples
```

## Where the rest of the documentation lives

- [Features](../features/index.md) — per-capability documentation and the capability matrix
- [Architecture](../architecture/index.md) — for people changing the code
- [Documentation map](../documentation-map.md) — which page is canonical for which policy
