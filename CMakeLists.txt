cmake_minimum_required(VERSION 3.20)
project(frame_hands CXX)

# Valve's OpenVR SDK (BSD-3-Clause), not part of this repository: scripts/build.ps1 clones it
# into third_party/openvr at the version below, or pass -DOPENVR_DIR=<your checkout>.
set(OPENVR_DIR "${CMAKE_SOURCE_DIR}/third_party/openvr" CACHE PATH "OpenVR SDK checkout (v2.15.6)")
if(NOT EXISTS "${OPENVR_DIR}/headers/openvr_driver.h")
    message(FATAL_ERROR "OpenVR SDK not found in ${OPENVR_DIR}. Run scripts/build.ps1, or clone "
                        "https://github.com/ValveSoftware/openvr (tag v2.15.6) there.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OPENVR_INCLUDE_DIR "${OPENVR_DIR}/headers")
set(OPENVR_LIBRARIES "${OPENVR_DIR}/lib/win64/openvr_api.lib")
set(PLATFORM 64)
set(ARCH_TARGET win64)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output/drivers")

# The OpenVR samples' small logging and math helpers (BSD-3-Clause, Valve).
add_subdirectory("${OPENVR_DIR}/samples/drivers/utils" utils)
add_subdirectory(driver)

option(FRAMEHANDS_TESTS "Build the tests" ON)
if(FRAMEHANDS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
