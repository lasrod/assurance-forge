# Assurance Forge Roadmap

This document outlines the major workstreams for Assurance Forge.

It is intended to guide development and provide visibility into project direction.
Each item represents a significant capability or architectural area.

---

## 📊 Status Definitions

| Status | Description |
|--------|------------|
| 🧠 Brainstorm | Identified but not yet evaluated |
| 🔍 Reviewing | Being analyzed and refined |
| 🧱 Planned | Accepted and ready for implementation |
| 🚧 In Progress | Currently under development |
| 🧪 Testing | Implemented and under validation |
| ✅ Completed | Finished and stable |
| ⏸ Deferred | Postponed |
| ❌ Rejected | Not pursued |

---

## 🧭 1. Product Definition

Define what Assurance Forge is and is not.

- Define MVP scope  
  Status: 🧠 Brainstorm  

- Define non-goals for initial versions  
  Status: 🧠 Brainstorm  

- Define core user workflows  
  Status: 🧠 Brainstorm  

---

## 🧩 2. SACM Strategy

Ensure credibility and correctness through SACM.

- Define supported SACM 2.3 subset  
  Status: 🧠 Brainstorm  

- Define import/export rules  
  Status: 🧠 Brainstorm  

- Define validation strategy  
  Status: 🧠 Brainstorm  

- Define handling of unsupported SACM elements  
  Status: 🧠 Brainstorm  

---

## 🧠 3. Domain Model

Create a robust internal representation of assurance cases.

- Design SACM-aligned class structure  
  Status: 🧠 Brainstorm  

- Define relationships and references  
  Status: 🧠 Brainstorm  

- Ensure separation between semantic model and tooling concerns  
  Status: 🧠 Brainstorm  

- Define extensibility for annotations and AI results  
  Status: 🧠 Brainstorm  

---

## 📥 4. XML Import

Load SACM data reliably.

- Select XML parsing approach  
  Status: 🧠 Brainstorm  

- Implement SACM XML parser  
  Status: 🧠 Brainstorm  

- Resolve references and dependencies  
  Status: 🧠 Brainstorm  

- Provide clear error diagnostics  
  Status: 🧠 Brainstorm  

---

## 📤 5. XML Export

Preserve and produce valid SACM output.

- Serialize internal model to SACM XML  
  Status: 🧠 Brainstorm  

- Ensure round-trip integrity (import → export)  
  Status: 🧠 Brainstorm  

- Preserve semantic meaning and structure  
  Status: 🧠 Brainstorm  

---

## 🧭 6. Visualization Model

Transform SACM into a readable structure.

- Define mapping from SACM to GSN-like representation  
  Status: 🧠 Brainstorm  

- Define node roles and relationships  
  Status: 🧠 Brainstorm  

- Handle structures not directly representable in GSN  
  Status: 🧠 Brainstorm  

---

## 📐 7. Layout Engine

Automatically generate readable layouts.

- Define layout rules for hierarchy  
  Status: 🧠 Brainstorm  

- Handle context, assumptions, and supporting elements  
  Status: 🧠 Brainstorm  

- Implement deterministic layout algorithm  
  Status: 🧠 Brainstorm  

- Handle spacing, overlap, and readability  
  Status: 🧠 Brainstorm  

- Ensure scalability for large safety cases  
  Status: 🧠 Brainstorm  

---

## 🖥️ 8. Rendering (ImGui)

Visualize assurance cases.

- Implement canvas and navigation  
  Status: 🧠 Brainstorm  

- Render nodes and relationships  
  Status: 🧠 Brainstorm  

- Support zoom and pan  
  Status: 🧠 Brainstorm  

- Support selection and inspection  
  Status: 🧠 Brainstorm  

---

## 🧑‍💻 9. Interaction

Enable users to work with assurance cases.

- Load and display SACM files  
  Status: 🧠 Brainstorm  

- Inspect node details  
  Status: 🧠 Brainstorm  

- Navigate relationships  
  Status: 🧠 Brainstorm  

- Display validation and parsing feedback  
  Status: 🧠 Brainstorm  

---

## 🤖 10. AI Integration

Provide assistance while maintaining user control.

- Define AI provider abstraction  
  Status: 🧠 Brainstorm  

- Support user-configured AI providers  
  Status: 🧠 Brainstorm  

- Define prompt and evaluation pipeline  
  Status: 🧠 Brainstorm  

- Represent AI suggestions in the model  
  Status: 🧠 Brainstorm  

---

## 📏 11. SCCG Integration

Apply Safety Case Core Guidelines.

- Define SCCG rule representation  
  Status: 🧠 Brainstorm  

- Implement evaluation logic  
  Status: 🧠 Brainstorm  

- Attach results to model elements  
  Status: 🧠 Brainstorm  

- Provide user-visible explanations  
  Status: 🧠 Brainstorm  

---

## 🔐 12. Data, Security, and Trust

Ensure safe and predictable behavior.

- Define data handling policy  
  Status: 🧠 Brainstorm  

- Ensure user control over external communication  
  Status: 🧠 Brainstorm  

- Define AI data boundaries  
  Status: 🧠 Brainstorm  

- Ensure no unintended data leakage  
  Status: 🧠 Brainstorm  

---

## 🧪 13. Testing and Validation

Ensure correctness and reliability.

- Unit tests for domain model  
  Status: 🧠 Brainstorm  

- XML parsing and serialization tests  
  Status: 🧠 Brainstorm  

- Round-trip validation tests  
  Status: 🧠 Brainstorm  

- Layout stability tests  
  Status: 🧠 Brainstorm  

---

## 📦 14. Open Source Setup

Prepare for collaboration.

- Define license  
  Status: 🧠 Brainstorm  

- Define contribution process  
  Status: 🧠 Brainstorm  

- Create issue templates  
  Status: 🧠 Brainstorm  

- Establish project structure  
  Status: 🧠 Brainstorm  

---

## ⚙️ 15. Build and Tooling

Ensure reproducible development.

- Define build system structure  
  Status: 🧠 Brainstorm  

- Configure dependency management  
  Status: 🧠 Brainstorm  

- Setup CI pipeline  
  Status: 🧠 Brainstorm  

---

## 📚 16. Documentation

Support users and contributors.

- Write architecture documentation  
  Status: 🧠 Brainstorm  

- Create getting started guide  
  Status: 🧠 Brainstorm  

- Provide example SACM files  
  Status: 🧠 Brainstorm  

---

## 🧾 Notes

- Layout is automatically generated and not persisted
- SACM XML is the source of truth
- Visualization is derived from the model
- AI usage must always be user-controlled