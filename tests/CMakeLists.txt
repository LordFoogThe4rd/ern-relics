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

add_core_check(signatures game/pattern_tests.cpp "core;game" ern_game_memory)

add_core_check(relic_reader game/reader_tests.cpp "core;game;memory" ern_game_memory)

add_core_check(relic_writer game/writer_tests.cpp "core;game;memory" ern_game_memory)

add_core_check(relic_equip game/equip_tests.cpp "core;game;memory" ern_game_memory)

add_core_check(relic_validation relics/validation_tests.cpp "core;relics;rules" ern_relic_model)

add_core_check(presets relics/preset_tests.cpp "core;relics;presets" ern_game_memory)

add_core_check(drafts relics/draft_tests.cpp "core;relics;drafts" ern_game_memory)

add_core_check(fingerprints data/fingerprint_tests.cpp "core;data" ern_game_data)

add_core_check(preset_store data/preset_store_tests.cpp "core;data;presets" ern_game_data)

add_core_check(rotating_file data/rotating_file_tests.cpp "core;data" ern_game_data)

add_core_check(edit_journal data/edit_journal_tests.cpp "core;data" ern_game_data)
