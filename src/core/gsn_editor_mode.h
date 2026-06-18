#pragma once

namespace core {

// The editing mode of a GSN canvas (ADR-0007). One canvas and one layout engine
// serve both concrete arguments and GSN argument patterns; the mode gates which
// context-menu actions, decorators, operators, and validations apply.
//
//   Argument - a normal concrete GSN argument. Dialectic (challenge / counter /
//              defeated) editing is available.
//   Pattern  - a GSN argument-pattern definition. Dialectic editing is hidden in
//              the UI and rejected at the command layer; pattern-specific
//              abstractions (uninstantiated, optionality, multiplicity, choice)
//              apply instead.
enum class GsnEditorMode {
    Argument,
    Pattern,
};

} // namespace core
