find_package(Python3 COMPONENTS Interpreter)

# The packet format: packets made by the headset exporter's own code, parsed by the driver's.
add_executable(packet_test packet_test.cpp ../driver/src/hand_link.cpp)
target_include_directories(packet_test PRIVATE ../driver/src)
target_link_libraries(packet_test PRIVATE ws2_32)
if(Python3_FOUND)
    add_test(NAME make_packets
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/make_packets.py ${CMAKE_CURRENT_BINARY_DIR}/packets)
    add_test(NAME packet_test COMMAND packet_test ${CMAKE_CURRENT_BINARY_DIR}/packets)
    set_tests_properties(packet_test PROPERTIES DEPENDS make_packets)
    add_test(NAME exporter_test
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/exporter_test.py)
endif()

# Contact retargeting on a recording (tools/ft_record.py): retarget_test recording.txt
add_executable(retarget_test retarget_test.cpp ../driver/src/hand_simulation.cpp)
target_include_directories(retarget_test PRIVATE ../driver/src ${OPENVR_INCLUDE_DIR})
target_link_libraries(retarget_test PRIVATE util_vrmath)

# Keep the test programs out of the driver output folder.
set_target_properties(packet_test retarget_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_BINARY_DIR}/bin>)

# Live receiver check (not run by CTest): link_probe [seconds]
add_executable(link_probe link_probe.cpp ../driver/src/hand_link.cpp)
target_include_directories(link_probe PRIVATE ../driver/src)
target_link_libraries(link_probe PRIVATE ws2_32)
set_target_properties(link_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_BINARY_DIR}/bin>)
