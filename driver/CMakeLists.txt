# The SteamVR driver: <build>/output/drivers/framehands is the folder SteamVR loads.
set(TARGET_NAME framehands)
set(DRIVER_NAME "driver_${TARGET_NAME}")

add_library(${DRIVER_NAME} SHARED
        src/hmd_driver_factory.cpp
        src/device_provider.h
        src/device_provider.cpp
        src/controller_device_driver.h
        src/controller_device_driver.cpp
        src/hand_simulation.h
        src/hand_simulation.cpp
        src/hand_fit.h
        src/hand_fit.cpp
        src/hand_link.h
        src/hand_link.cpp
        )

# Straight into <output>/framehands/bin/win64 (no Release/Debug subfolder).
set_target_properties(${DRIVER_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/bin/${ARCH_TARGET}>)

target_link_libraries(${DRIVER_NAME} PRIVATE ${OPENVR_LIBRARIES} util_driverlog util_vrmath ws2_32)
target_include_directories(${DRIVER_NAME} PRIVATE ${OPENVR_INCLUDE_DIR})
target_compile_definitions(${DRIVER_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)

# The driver's manifest and resources next to the DLL.
add_custom_command(
        TARGET ${DRIVER_NAME}
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}
)
