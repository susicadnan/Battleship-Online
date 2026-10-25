# Writes netplay_build_id.h: the commits of this repo and of decomp, which is
# what two games must share to play online (port/netplay/lobby.cpp compares
# them on connect). Runs on every build; the header is only rewritten when the
# id changes, so nothing recompiles needlessly.
# Inputs: SRC_DIR (repo root), OUT_FILE.
set(id "")
foreach(dir "${SRC_DIR}" "${SRC_DIR}/decomp")
    execute_process(
        COMMAND git -C "${dir}" rev-parse --short=10 HEAD
        OUTPUT_VARIABLE rev
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE res)
    if(NOT res EQUAL 0 OR rev STREQUAL "")
        set(rev "unknown")
    endif()
    if(id STREQUAL "")
        set(id "${rev}")
    else()
        set(id "${id}-${rev}")
    endif()
endforeach()
set(content "#pragma once\n#define PORT_NETPLAY_BUILD_ID \"${id}\"\n")
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" old)
else()
    set(old "")
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT_FILE}" "${content}")
endif()
