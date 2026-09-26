# run_sccg_eval_last_run_test.cmake -- drive af-sccg-review-eval at the top of
# the run-number range, as a dry run so no provider is called.
#
# `--first-run 2147483647 --runs 1` is a valid request for exactly one run. The
# run loop once counted by run number, so `++run` past INT_MAX overflowed and
# the harness went on writing runs numbered from INT_MIN until killed. The
# option parser's unit tests could not see that; only the real loop can.
#
# Inputs: AF_EVAL_EXE, AF_EVAL_PROJECT, AF_EVAL_ELEMENT, AF_EVAL_OUT_DIR.

foreach(_input AF_EVAL_EXE AF_EVAL_PROJECT AF_EVAL_ELEMENT AF_EVAL_OUT_DIR)
    if(NOT DEFINED ${_input})
        message(FATAL_ERROR "${_input} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${AF_EVAL_OUT_DIR}")

# The timeout is the assertion that the loop stops: a correct run takes about a
# second, and the overflowing one never finishes.
execute_process(
    COMMAND "${AF_EVAL_EXE}"
            --project "${AF_EVAL_PROJECT}"
            --element "${AF_EVAL_ELEMENT}"
            --dry-run
            --first-run 2147483647
            --runs 1
            --out "${AF_EVAL_OUT_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    TIMEOUT 60
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "af-sccg-review-eval exited with '${_result}'\nstdout:\n${_output}\nstderr:\n${_error}")
endif()

file(GLOB _runs RELATIVE "${AF_EVAL_OUT_DIR}" "${AF_EVAL_OUT_DIR}/*--run*.json")
set(_expected "${AF_EVAL_ELEMENT}--run2147483647.json")
if(NOT _runs STREQUAL _expected)
    message(FATAL_ERROR "Expected exactly '${_expected}', found: '${_runs}'")
endif()
