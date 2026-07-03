// Small CLI test utility for the SACM 2.3 library (SACM23-CLI-001).
// Subcommands grow with the library slices; keep this a thin shell over
// the public API.

#include "sacm/version.h"

#include <cstring>
#include <iostream>
#include <string_view>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;

int print_usage(std::ostream& out) {
    out << "usage: sacm_cli <command>\n"
           "\n"
           "commands:\n"
           "  version    print library and SACM standard versions\n";
    return kExitUsage;
}

int run_version() {
    std::cout << "sacm library " << sacm::library_version() << " (SACM " << sacm::standard_version()
              << ")\n";
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
    std::cerr << "error: unknown command: " << command << "\n";
    return print_usage(std::cerr);
}
