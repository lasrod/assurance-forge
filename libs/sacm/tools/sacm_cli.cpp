// Small CLI test utility for the SACM 2.3 library (SACM23-CLI-001).
// Thin shell over the public API: version, validate, roundtrip, export.

#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/validation/validate.h"
#include "sacm/version.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitErrors = 1;
constexpr int kExitUsage = 2;

int print_usage(std::ostream& out) {
    out << "usage: sacm_cli <command> [arguments]\n"
           "\n"
           "commands:\n"
           "  version                          print library and SACM standard versions\n"
           "  validate <file> [--strict] [--json]\n"
           "                                   load a SACM XMI file and report diagnostics\n"
           "  roundtrip <file> [--strict]      load -> strict save -> reload -> semantic compare\n"
           "  export <in> [-o <out>] [--strict]\n"
           "                                   load and emit deterministic strict SACM 2.3 XMI\n"
           "\n"
           "Loading is tolerant by default; --strict enforces strict SACM 2.3 on load.\n";
    return kExitUsage;
}

std::string json_escape(std::string_view text) {
    std::string escaped;
    for (const char c : text) {
        switch (c) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(c);
        }
    }
    return escaped;
}

void print_diagnostics(const std::vector<sacm::validation::Diagnostic>& diagnostics, bool json) {
    if (json) {
        std::cout << "[";
        bool first = true;
        for (const auto& diagnostic : diagnostics) {
            if (!first) {
                std::cout << ",";
            }
            first = false;
            std::cout << "\n  {\"code\": \"" << json_escape(diagnostic.code) << "\", \"severity\": \""
                      << sacm::validation::severity_name(diagnostic.severity)
                      << "\", \"requirement\": \"" << json_escape(diagnostic.requirement_id)
                      << "\", \"message\": \"" << json_escape(diagnostic.message) << "\"}";
        }
        std::cout << (diagnostics.empty() ? "]" : "\n]") << "\n";
        return;
    }
    for (const auto& diagnostic : diagnostics) {
        std::cout << sacm::validation::severity_name(diagnostic.severity) << " " << diagnostic.code
                  << " [" << diagnostic.requirement_id << "] " << diagnostic.message;
        if (diagnostic.location.has_value()) {
            std::cout << " (" << diagnostic.location->file << ":" << diagnostic.location->line
                      << ")";
        }
        std::cout << "\n";
    }
}

struct CommonArgs {
    std::string file;
    std::string output;
    bool strict = false;
    bool json = false;
    bool ok = false;
};

CommonArgs parse_args(int argc, char** argv) {
    CommonArgs args;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--strict") {
            args.strict = true;
        } else if (arg == "--tolerant") {
            args.strict = false;
        } else if (arg == "--json") {
            args.json = true;
        } else if (arg == "-o" && i + 1 < argc) {
            args.output = argv[++i];
        } else if (!arg.starts_with("-") && args.file.empty()) {
            args.file = arg;
        } else {
            std::cerr << "error: unknown argument: " << arg << "\n";
            return args;
        }
    }
    args.ok = !args.file.empty();
    if (!args.ok) {
        std::cerr << "error: missing input file\n";
    }
    return args;
}

sacm::io::LoadOptions load_options(const CommonArgs& args) {
    return sacm::io::LoadOptions{
        .mode = args.strict ? sacm::io::Mode::Strict : sacm::io::Mode::Tolerant};
}

int run_version() {
    std::cout << "sacm library " << sacm::library_version() << " (SACM " << sacm::standard_version()
              << ")\n";
    return kExitOk;
}

int run_validate(const CommonArgs& args) {
    sacm::io::LoadResult loaded = sacm::io::load_xmi_file(args.file, load_options(args));
    std::vector<sacm::validation::Diagnostic> diagnostics = std::move(loaded.diagnostics);
    if (loaded.document.has_value()) {
        std::vector<sacm::validation::Diagnostic> semantic =
            sacm::validation::validate(*loaded.document);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(semantic.begin()),
                           std::make_move_iterator(semantic.end()));
    }
    print_diagnostics(diagnostics, args.json);
    const bool has_errors = sacm::validation::has_errors(diagnostics);
    if (!args.json) {
        std::cout << (has_errors ? "INVALID" : "VALID") << " " << args.file << "\n";
    }
    return has_errors ? kExitErrors : kExitOk;
}

int run_roundtrip(const CommonArgs& args) {
    const sacm::io::LoadResult first = sacm::io::load_xmi_file(args.file, load_options(args));
    print_diagnostics(first.diagnostics, false);
    if (!first.document.has_value() || sacm::validation::has_errors(first.diagnostics)) {
        std::cout << "ROUNDTRIP FAILED (load) " << args.file << "\n";
        return kExitErrors;
    }
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*first.document);
    if (!saved.ok) {
        print_diagnostics(saved.diagnostics, false);
        std::cout << "ROUNDTRIP FAILED (save) " << args.file << "\n";
        return kExitErrors;
    }
    const sacm::io::LoadResult second = sacm::io::load_xmi_string(saved.xml);
    if (!second.document.has_value() || sacm::validation::has_errors(second.diagnostics)) {
        print_diagnostics(second.diagnostics, false);
        std::cout << "ROUNDTRIP FAILED (reload) " << args.file << "\n";
        return kExitErrors;
    }
    const std::vector<sacm::compare::SemanticDifference> differences =
        sacm::compare::semantic_compare(*first.document, *second.document);
    for (const auto& difference : differences) {
        std::cout << "difference [" << difference.category << "] " << difference.path << ": "
                  << difference.message << "\n";
    }
    if (differences.empty()) {
        std::cout << "ROUNDTRIP OK " << args.file << "\n";
        return kExitOk;
    }
    std::cout << "ROUNDTRIP FAILED (" << differences.size() << " difference(s)) " << args.file
              << "\n";
    return kExitErrors;
}

int run_export(const CommonArgs& args) {
    const sacm::io::LoadResult loaded = sacm::io::load_xmi_file(args.file, load_options(args));
    print_diagnostics(loaded.diagnostics, false);
    if (!loaded.document.has_value() || sacm::validation::has_errors(loaded.diagnostics)) {
        return kExitErrors;
    }
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    if (!saved.ok) {
        print_diagnostics(saved.diagnostics, false);
        return kExitErrors;
    }
    if (args.output.empty()) {
        std::cout << saved.xml;
    } else {
        std::ofstream out(args.output, std::ios::binary);
        if (!out) {
            std::cerr << "error: cannot write " << args.output << "\n";
            return kExitErrors;
        }
        out << saved.xml;
    }
    return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return print_usage(std::cerr);
    }
    const std::string_view command = argv[1];
    if (command == "version" || command == "--version") {
        return run_version();
    }
    if (command == "--help" || command == "-h" || command == "help") {
        print_usage(std::cout);
        return kExitOk;
    }
    if (command == "validate" || command == "roundtrip" || command == "export") {
        const CommonArgs args = parse_args(argc, argv);
        if (!args.ok) {
            return kExitUsage;
        }
        if (command == "validate") {
            return run_validate(args);
        }
        if (command == "roundtrip") {
            return run_roundtrip(args);
        }
        return run_export(args);
    }
    std::cerr << "error: unknown command: " << command << "\n";
    return print_usage(std::cerr);
}
