# SACM agent and interoperability research notes

## Official SACM 2.3 references

- SACM 2.3 specification page: https://www.omg.org/spec/SACM/2.3/About-SACM
- Specification PDF: https://www.omg.org/spec/SACM/2.3/PDF, OMG file ID `formal/23-05-08`
- Normative machine-readable SACM XML: https://www.omg.org/spec/SACM/20220301/SACM.xml, OMG file ID `ptc/22-03-13`
- Informative SACM 2.3 UML Profile archive: OMG file ID `ptc/22-03-14`

Do not bundle OMG artifacts in this agent pack. Use `scripts/fetch-sacm23-references.sh` when local pinning is desired and project policy permits it.

## Agent platform notes

Claude Code project subagents are appropriate for this workflow because they can be checked into the repository under `.claude/agents/` and specialized by task. This pack uses project-local agents so the workflow travels with the codebase.

The agent split mirrors a safety-critical development process:

- Lead coordinates the workflow.
- Specification analyst extracts obligations.
- Metamodel cartographer checks the machine-readable standard.
- Architect protects the reusable library boundary.
- Test engineer writes failing tests.
- Implementer changes code.
- Verifier independently challenges the result.
- Adapter engineer integrates Assurance Forge without weakening the library.
- Interop researcher builds a corpus of real-world examples.

## Similar or adjacent tool efforts

These are useful references, not normative authorities:

| Candidate | URL | Relevance | Use |
|---|---|---|---|
| Systems Assurance Group SACM materials | https://github.com/SystemsAssuranceGroup/SACM | SACM metamodel material, EMF/Papyrus-oriented work. | Compare model coverage and generate sample ideas. |
| wrwei/SACM | https://github.com/wrwei/SACM | EMF implementation and SACM 2.3 metamodel artifacts. | Cross-check implementation inventory. |
| AdvoCATE | FAA/NASA public papers and tool references | Assurance-case construction and format translation. | Interoperability and workflow lessons. |
| PREMIS/NOR-STA | https://www.argevide.com/assurance-case/ | Modular assurance cases and SACM-related claims. | Package interface/binding interoperability ideas. |
| SmartGSN | https://arxiv.org/abs/2410.16675 | LLM-assisted GSN assurance-case management. | Agent UX ideas, not SACM conformance. |
| Open Autonomy Safety Case | https://github.com/EdgeCaseResearch/oasc | Public safety-case material referenced by Assurance Forge docs. | Possible corpus candidate after license review. |
| OMG Model Interchange Working Group | https://www.omgwiki.org/model-interchange/ | Model interchange testing discipline and valid XMI files for related OMG technologies. | Test-corpus method inspiration. |

## Research conclusion

A generic code agent is not enough. SACM 2.3 compliance requires a traceable, test-first, XMI-focused workflow with independent verification. Similar tools and metamodel repositories can provide examples and interoperability risks, but the reusable library must be driven by the official SACM 2.3 specification and normative machine-readable model.

## Open research tasks

- Identify exact XMI namespace/version behavior required by SACM 2.3 from the PDF and normative XML.
- Build a list of all concrete SACM 2.3 metaclasses and inherited fields.
- Identify at least one public example for each major compliance area: packages, terminology, argumentation, artifacts.
- Determine whether third-party examples can be committed, minimized, or referenced only.
- Record known behavior of Enterprise Architect, Papyrus/EMF, PREMIS/NOR-STA, AdvoCATE, OpenCert, and other relevant tools if examples are available.
