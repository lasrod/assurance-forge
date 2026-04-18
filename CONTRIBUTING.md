## 🧱 Engineering Principles

Assurance Forge is a model-driven tool for safety case development.

The architecture should support correctness, traceability, and user trust.

### Core Principles

- SACM XML represents the assurance case and is treated as the source of truth
- The tool must not introduce ambiguity or alter the meaning of assurance data
- Users must be able to clearly separate their production data from the tool itself
- Visualization and tooling must always be derived from the underlying model
- The system should remain deterministic and reproducible

---

### Data and Trust

- User data belongs to the user at all times
- No data may be sent externally without explicit user consent
- AI integrations must be transparent and user-controlled
- The tool must not silently modify or reinterpret safety arguments

---

### Quality and Testing

- Core functionality must be covered by tests
- Changes to parsing, serialization, or model handling require validation
- Round-trip integrity (import → export) should be preserved where possible

---

### Design Direction

When contributing, aim to:

- Keep the architecture simple and modular
- Maintain a clear separation of concerns
- Avoid introducing hidden behavior or implicit assumptions
- Ensure the tool scales to large and complex safety cases

---

### Scope Awareness

Assurance Forge is not a general-purpose diagram editor.

Contributions should reinforce its role as a safety case engineering tool.