cmake_minimum_required(VERSION 3.22)

set(revision "unavailable")
set(dirty "unknown")
# A source archive can itself be unpacked inside an unrelated Git working tree.
# A linked Git worktree also has a .git entry (a file instead of a directory).
if(GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE revision_status
        OUTPUT_VARIABLE head
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(revision_status STREQUAL "0")
        set(revision "${head}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${SOURCE_DIR}"
            RESULT_VARIABLE status_result
            OUTPUT_VARIABLE changes
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(status_result STREQUAL "0")
            if(changes STREQUAL "")
                set(dirty "false")
            else()
                set(dirty "true")
            endif()
        endif()
    endif()
endif()

file(CONFIGURE OUTPUT "${OUTPUT_HEADER}" CONTENT [=[
#define SATCOMFEC_BENCHMARK_GIT_SHA "@revision@"
#define SATCOMFEC_BENCHMARK_GIT_DIRTY "@dirty@"
]=] @ONLY)
