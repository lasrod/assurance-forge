// assurance-forge-mcp — the Model Context Protocol server for Assurance Forge.
//
// Launched by an MCP client (Claude Desktop, Claude Code, Cursor), which speaks
// JSON-RPC 2.0 over this process's stdin and stdout.
//
// STDOUT IS THE TRANSPORT. Nothing may write to it except the protocol. Every
// diagnostic in this file goes to stderr, and on Windows stdout is switched to
// binary so the CRT does not rewrite "\n" as "\r\n" inside the framing.

#include "mcp/server.h"
#include "mcp/session.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;

void PrintUsage() {
    std::cerr << "assurance-forge-mcp — Model Context Protocol server for Assurance Forge\n\n"
                 "Usage:\n"
                 "  assurance-forge-mcp --project <path> [--settings <path>]\n"
                 "  assurance-forge-mcp --version\n\n"
                 "  --project   Project directory, project manifest, or a SACM file.\n"
                 "  --settings  Override the settings file used for the consent gate.\n\n"
                 "The server speaks JSON-RPC 2.0 over stdin/stdout and is meant to be\n"
                 "launched by an MCP client, not run interactively.\n";
}

void UseBinaryStdout() {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    mcp::Session::Config config;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& argument = args[index];
        if (argument == "--help" || argument == "-h") {
            PrintUsage();
            return kExitOk;
        }
        if (argument == "--version") {
            std::cerr << mcp::kServerName << ' ' << mcp::kServerVersion << " (MCP " << mcp::kProtocolVersion << ")\n";
            return kExitOk;
        }
        if (argument == "--project" && index + 1 < args.size()) {
            config.project_path = args[++index];
            continue;
        }
        if (argument == "--settings" && index + 1 < args.size()) {
            config.settings_path = args[++index];
            continue;
        }
        std::cerr << "Unrecognized or incomplete argument: " << argument << "\n\n";
        PrintUsage();
        return kExitUsage;
    }

    if (config.project_path.empty()) {
        if (const char* from_environment = std::getenv("AF_MCP_PROJECT")) {
            config.project_path = from_environment;
        }
    }

    std::string error;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    if (session == nullptr) {
        std::cerr << "assurance-forge-mcp: " << error << '\n';
        return kExitUsage;
    }

    UseBinaryStdout();

    mcp::Server server(*session);
    server.Run(std::cin, std::cout);
    return kExitOk;
}
