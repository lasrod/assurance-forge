# --------------------------------------------------------------------
# run_mcp_smoke_test.cmake — drive assurance-forge-mcp over real pipes.
#
# The unit tests exercise Server::HandleMessage directly, which proves the
# dispatch logic but not the thing most likely to break in the field: the stdio
# transport itself. A stray write to stdout from any linked layer, or CRLF
# translation on Windows, corrupts the stream and presents to the user as a
# broken client. That only shows up when the real process is driven through real
# pipes, which is what this does.
#
# Usage:
#   cmake -DAF_MCP_EXE=<path> -DAF_MCP_PROJECT=<path> -DAF_MCP_INPUT=<jsonl>
#         -DAF_MCP_MODE=<granted|withheld> -DAF_MCP_WORK_DIR=<dir>
#         -P cmake/run_mcp_smoke_test.cmake
# --------------------------------------------------------------------

foreach(required AF_MCP_EXE AF_MCP_PROJECT AF_MCP_INPUT AF_MCP_MODE AF_MCP_WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_mcp_smoke_test.cmake: -D${required} is required")
    endif()
endforeach()

# The consent gate reads a settings file. Writing it here rather than committing
# a fixture keeps the test from ever reading the developer's real settings, where
# a locally enabled MCP would make the withheld case pass spuriously.
set(_settings "${AF_MCP_WORK_DIR}/mcp_smoke_settings_${AF_MCP_MODE}.json")
if(AF_MCP_MODE STREQUAL "granted")
    file(WRITE "${_settings}" "{\"mcp\":{\"enabled\":true}}")
elseif(AF_MCP_MODE STREQUAL "withheld")
    file(WRITE "${_settings}" "{\"mcp\":{\"enabled\":false}}")
else()
    message(FATAL_ERROR "AF_MCP_MODE must be 'granted' or 'withheld', got '${AF_MCP_MODE}'")
endif()

# Captured to a FILE, not to OUTPUT_VARIABLE. execute_process normalizes CRLF to
# LF on the way into a variable, which silently destroys the evidence the
# carriage-return assertion below depends on -- with OUTPUT_VARIABLE that check
# passes even against a server that really is emitting CRLF. Measured, not
# assumed.
set(_stdout_file "${AF_MCP_WORK_DIR}/mcp_smoke_stdout_${AF_MCP_MODE}.txt")
execute_process(
    COMMAND "${AF_MCP_EXE}" --project "${AF_MCP_PROJECT}" --settings "${_settings}"
    INPUT_FILE "${AF_MCP_INPUT}"
    OUTPUT_FILE "${_stdout_file}"
    ERROR_VARIABLE _stderr
    RESULT_VARIABLE _exit_code
)

if(NOT _exit_code EQUAL 0)
    message(FATAL_ERROR "assurance-forge-mcp exited ${_exit_code}\nstderr:\n${_stderr}")
endif()

file(READ "${_stdout_file}" _stdout)
file(READ "${_stdout_file}" _stdout_hex HEX)

# --- Framing ---
# Three requests and one notification go in; a notification must draw no reply,
# so exactly three lines must come back. This is the assertion that catches
# stdout pollution: anything else written to the stream changes the line count.
string(REGEX MATCHALL "\n" _newlines "${_stdout}")
list(LENGTH _newlines _line_count)
if(NOT _line_count EQUAL 3)
    message(FATAL_ERROR
        "Expected exactly 3 response lines (a notification must not be answered), got ${_line_count}.\n"
        "This usually means something wrote to stdout that is not the protocol.\n"
        "stdout:\n${_stdout}")
endif()

# Counting newlines does NOT catch CRLF -- "}\r\n" still holds exactly one \n, so
# the check above passes on a stream that breaks any client which does not strip
# \r. `UseBinaryStdout` in src/mcp/main.cpp stops the CRT rewriting the framing on
# Windows; this is what holds that in place, and removing it fails this test.
#
# Split into bytes rather than searched as text: the hex dump is one continuous
# string, so a naive FIND for "0d" could straddle a byte boundary and report a
# carriage return that is not there.
string(REGEX REPLACE "(..)" ";\\1" _stdout_bytes "${_stdout_hex}")
if("0d" IN_LIST _stdout_bytes)
    message(FATAL_ERROR
        "stdout contains a carriage return; the transport must emit bare newlines.\n"
        "This usually means the binary-mode stdout setup in src/mcp/main.cpp was lost.\n"
        "stdout:\n${_stdout}")
endif()

# --- Handshake and tool advertisement ---
foreach(_needle "\"protocolVersion\":\"2025-06-18\"" "\"name\":\"assurance-forge\""
                "get_case_overview" "find_elements" "get_element" "get_argument_tree")
    string(FIND "${_stdout}" "${_needle}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Response did not contain ${_needle}\nstdout:\n${_stdout}")
    endif()
endforeach()

# --- Consent ---
# The needles below are deliberately unquoted. A tool payload travels as a JSON
# *string* inside the result, so its own quotes arrive escaped -- the bytes on the
# wire are \"element_count\", and a needle written as "element_count" can never
# match. That mattered: the leak assertion in the withheld branch was passing
# because it could not match anything, not because nothing leaked.
if(AF_MCP_MODE STREQUAL "granted")
    string(FIND "${_stdout}" "element_count" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR
            "Consent was granted but no case content came back.\nstdout:\n${_stdout}")
    endif()
else()
    string(FIND "${_stdout}" "element_count" _leaked)
    if(NOT _leaked EQUAL -1)
        message(FATAL_ERROR
            "Consent was withheld and case content leaked anyway.\nstdout:\n${_stdout}")
    endif()
    # The case name is not a field name, so it cannot appear by coincidence in a
    # refusal: a second, independent witness that no content came back.
    string(FIND "${_stdout}" "VehicleBrakingAssuranceCase" _leaked_name)
    if(NOT _leaked_name EQUAL -1)
        message(FATAL_ERROR
            "Consent was withheld and the case name leaked anyway.\nstdout:\n${_stdout}")
    endif()
    string(FIND "${_stdout}" "has not been given permission" _explained)
    if(_explained EQUAL -1)
        message(FATAL_ERROR
            "Consent was withheld but the refusal did not say how to grant it.\nstdout:\n${_stdout}")
    endif()
endif()

message(STATUS "MCP stdio smoke test passed (mode: ${AF_MCP_MODE}).")
