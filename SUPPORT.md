# Support

Assurance Forge is maintained by one person alongside other work. Everything
here is best-effort — there is no commercial support offering and no response-time
guarantee.

## Where to go

| I want to… | Go here |
|---|---|
| Ask how to do something, or check whether the tool can do it | [Discussions → Q&A](https://github.com/lasrod/assurance-forge/discussions/categories/q-a) |
| Suggest a feature or share an idea | [Discussions → Ideas](https://github.com/lasrod/assurance-forge/discussions/categories/ideas) |
| Report something broken | [Open a bug report](https://github.com/lasrod/assurance-forge/issues/new/choose) |
| Report a security vulnerability | **Not an issue** — see [SECURITY.md](SECURITY.md) |
| Contribute code | [CONTRIBUTING.md](CONTRIBUTING.md) |
| Find out whether a capability exists | [Capability matrix](docs/features/feature-matrix.md) |
| Understand SACM conformance | [SACM 2.3 conformance matrix](docs/sacm/sacm-conformance-matrix.md) |

## Before reporting a bug

Most of the value in a bug report is in what makes it reproducible. Please
include:

- What you did, what happened, and what you expected instead.
- Your OS and the Assurance Forge version or commit.
- Whether you used a release binary or built from source.
- A minimal SACM or project file that triggers it, if the problem involves a
  particular file.

**Please redact anything confidential.** Assurance cases routinely contain
proprietary system detail. A cut-down file that still reproduces the problem is
more useful than a complete one you have to sanitize under pressure — and
attachments on a public issue cannot be reliably unpublished.

## Data-loss and misinterpretation reports get priority

If Assurance Forge loses content from your safety case, silently changes it, or
displays something the source file does not say, that is the most serious class
of bug this project has. Say so plainly in the report and it will be treated
accordingly. Keep the original file unmodified if you can — it is the evidence.

## What is not supported

- **Safety or regulatory advice.** This project cannot tell you whether your
  safety case is adequate, or whether it satisfies a standard or a regulator.
- **Certification support.** Assurance Forge is not certified or qualified for
  use in any regulated process. See
  [Status and limitations](README.md#status-and-limitations).
- **Older releases.** Only the latest release is maintained.
- **Platforms other than the built ones.** CI builds Windows, Linux and macOS;
  pre-built binaries are Windows x64 only. Other platforms may work but are not
  tested.
