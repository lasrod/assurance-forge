# Assurance Forge

Assurance Forge is an open-source tool for building, reviewing and navigating
safety cases. Engineers work in Goal Structuring Notation (GSN); the case is
stored as standard SACM 2.3 XMI.

## Where things stand

Assurance Forge is being rebuilt in Rust. This site describes that work. The
rebuild opens a SACM 2.3 file and draws its argument as a GSN diagram, in a
desktop application and in the browser. Editing has begun: elements can be
added, with undo and redo; saving comes next.

| You want | Go to |
|---|---|
| A tool to use today | The C++ application: [release 0.3.0-alpha.3](https://github.com/lasrod/assurance-forge/releases/tag/0.3.0-alpha.3). Its documentation is on the [`cpp` branch](https://github.com/lasrod/assurance-forge/tree/cpp/docs). |
| The plan for the rebuild | [Rust rebuild, Phase 0](roadmap/rust-rebuild-phase-0.md) |
| The step that built the viewer | [Milestone 1, step 1: the diagram](roadmap/milestone-1-diagram.md) |
| The problem list | [Milestone 1, step 2: the problem list](roadmap/milestone-1-problems.md) |
| The first editing step | [Milestone 2, step 1: adding elements](roadmap/milestone-2-add-elements.md) |
| Saving | [Milestone 2, step 2: saving](roadmap/milestone-2-save.md) |
| The recovery copy | [Milestone 2, step 3: the recovery copy](roadmap/milestone-2-recovery.md) |
| Moving an element | [Milestone 2, step 4: moving an element](roadmap/milestone-2-move.md) |
| The shape of the new code | [`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md) |
| How GSN is expressed in SACM | [SACM to GSN mapping](standards/sacm-gsn-mapping.md) |
| What the C++ version can do | [Capability matrix](reference/cpp-capability-matrix.md) |

## What is on this site

- **Roadmap.** What was decided for the rebuild and what comes next.
- **Standards.** How GSN maps onto SACM, with the evidence for each mapping, and
  where the two metamodels do not line up. These hold for any implementation.
- **C++ version.** The capability and GSN conformance matrices of the C++
  application as of release 0.3.0-alpha.3. They are the reference for what the
  Rust tool has to match, and they are not maintained here.

The SACM library itself is a separate project:
[libSACM](https://github.com/lasrod/libSACM).
