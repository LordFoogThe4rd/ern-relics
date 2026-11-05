function(add_core_check name source labels target)
    add_executable(ern_${name}_tests ${source})
    set_target_properties(ern_${name}_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(ern_${name}_tests PRIVATE ${target})
    target_include_directories(ern_${name}_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    ern_configure_target(ern_${name}_tests)
    add_test(NAME ${name} COMMAND ern_${name}_tests)
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "${labels}" TIMEOUT 30)
endfunction()

add_core_check(relic_validation relics/validation_tests.cpp "core;relics;rules" ern_relic_model)
