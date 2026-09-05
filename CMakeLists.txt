cmake_minimum_required(VERSION 3.20)

project(can_vehicle_telemetry VERSION 1.0.0 LANGUAGES C CXX)

option(TELEMETRY_BUILD_TESTS "Build host-side unit tests" ON)
option(TELEMETRY_BUILD_SIMULATOR "Build the host fault-injection simulator" ON)
option(TELEMETRY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(TELEMETRY_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(telemetry_protocol STATIC
    src/protocol.c
    src/receiver.c
)

target_include_directories(telemetry_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(telemetry_protocol PUBLIC c_std_11)

if(MSVC)
    target_compile_options(telemetry_protocol PRIVATE /W4)
    if(TELEMETRY_WARNINGS_AS_ERRORS)
        target_compile_options(telemetry_protocol PRIVATE /WX)
    endif()
else()
    target_compile_options(telemetry_protocol PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    if(TELEMETRY_WARNINGS_AS_ERRORS)
        target_compile_options(telemetry_protocol PRIVATE -Werror)
    endif()
endif()

if(TELEMETRY_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(telemetry_protocol PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(telemetry_protocol PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(TELEMETRY_BUILD_SIMULATOR)
    add_executable(telemetry_sim apps/simulator.cpp)
    target_link_libraries(telemetry_sim PRIVATE telemetry_protocol)
    target_compile_features(telemetry_sim PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(telemetry_sim PRIVATE /W4)
        if(TELEMETRY_WARNINGS_AS_ERRORS)
            target_compile_options(telemetry_sim PRIVATE /WX)
        endif()
    else()
        target_compile_options(telemetry_sim PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(TELEMETRY_WARNINGS_AS_ERRORS)
            target_compile_options(telemetry_sim PRIVATE -Werror)
        endif()
    endif()
endif()

if(TELEMETRY_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(telemetry_tests tests/test_telemetry.c)
    target_link_libraries(telemetry_tests PRIVATE telemetry_protocol)

    # Compile the real adapter against a deliberately small host HAL double.
    # This does not build the Cube integration example or validate hardware.
    add_executable(telemetry_hal_tests
        tests/test_stm32_hal.c
        platform/stm32f446re/telemetry_stm32_hal.c
    )
    target_link_libraries(telemetry_hal_tests PRIVATE telemetry_protocol)
    target_include_directories(telemetry_hal_tests PRIVATE
        tests/mocks platform/stm32f446re
    )

    foreach(test_target IN ITEMS telemetry_tests telemetry_hal_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /W4)
            if(TELEMETRY_WARNINGS_AS_ERRORS)
                target_compile_options(${test_target} PRIVATE /WX)
            endif()
        else()
            target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
            if(TELEMETRY_WARNINGS_AS_ERRORS)
                target_compile_options(${test_target} PRIVATE -Werror)
            endif()
        endif()
    endforeach()

    add_test(NAME telemetry_unit_tests COMMAND telemetry_tests)
    add_test(NAME telemetry_hal_integration_tests COMMAND telemetry_hal_tests)

    if(TELEMETRY_BUILD_SIMULATOR)
        foreach(fault IN ITEMS none crc range sequence timeout can)
            add_test(NAME telemetry_sim_${fault}
                COMMAND telemetry_sim --frames 6 --fault ${fault} --fault-at 2)
        endforeach()
        set_tests_properties(telemetry_sim_none PROPERTIES PASS_REGULAR_EXPRESSION
            "SUMMARY accepted=6 crc=0 range=0 sequence=0 timeout=0 can=0")
        set_tests_properties(telemetry_sim_crc PROPERTIES PASS_REGULAR_EXPRESSION
            "SUMMARY accepted=4 crc=1 range=0 sequence=1 timeout=0 can=0")
        set_tests_properties(telemetry_sim_range PROPERTIES PASS_REGULAR_EXPRESSION
            "SUMMARY accepted=4 crc=0 range=1 sequence=1 timeout=0 can=0")
        set_tests_properties(telemetry_sim_sequence PROPERTIES PASS_REGULAR_EXPRESSION
            "SUMMARY accepted=5 crc=0 range=0 sequence=1 timeout=0 can=0")
        set_tests_properties(telemetry_sim_timeout PROPERTIES PASS_REGULAR_EXPRESSION
            "SUMMARY accepted=6 crc=0 range=0 sequence=0 timeout=1 can=0")
        set_tests_properties(telemetry_sim_can PROPERTIES PASS_REGULAR_EXPRESSION
            "SUMMARY accepted=4 crc=0 range=0 sequence=1 timeout=0 can=1")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS telemetry_protocol
    EXPORT telemetry_protocol_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
