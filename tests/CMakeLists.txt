set(Python3_EXECUTABLE "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe" CACHE FILEPATH "Python interpreter for development checks")
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)

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

add_core_check(deep_relics relics/deep_tests.cpp "app;relics;rules;memory" ern_application)

add_core_check(fingerprints data/fingerprint_tests.cpp "core;data" ern_game_data)

add_core_check(active_regulation data/regulation_tests.cpp "core;data;rules" ern_application)

add_core_check(preset_store data/preset_store_tests.cpp "core;data;presets" ern_game_data)

add_core_check(rotating_file data/rotating_file_tests.cpp "core;data" ern_game_data)

add_core_check(edit_journal data/edit_journal_tests.cpp "core;data" ern_game_data)

add_core_check(event_log app/event_log_tests.cpp "app;data" ern_application)

add_core_check(editor_session app/editor_session_tests.cpp "app;relics;memory" ern_application)

add_core_check(relic_edit_queue app/relic_edit_queue_tests.cpp "app;memory" ern_application)

add_core_check(native_edit_diagnostics app/native_edit_diagnostics_tests.cpp "app;memory" ern_application)

add_core_check(native_callback_hook app/native_callback_hook_tests.cpp "app;memory" ern_application)

add_core_check(process_guard app/process_guard_tests.cpp "app;startup" ern_application)

add_core_check(relic_commit app/relic_commit_tests.cpp "app;memory" ern_application)

add_core_check(regulation_worker app/regulation_worker_tests.cpp "app;data;rules" ern_application)

add_core_check(runtime app/runtime_tests.cpp "app;memory;rules" ern_application)

add_core_check(relic_undo app/undo_tests.cpp "app;memory;rules" ern_application)

add_core_check(runtime_session app/runtime_session_tests.cpp "app;memory;session" ern_application)

add_core_check(editor_application app/editor_application_tests.cpp "app;memory;presets;session" ern_application)

add_core_check(render_lifecycle app/render_lifecycle_tests.cpp "app;graphics" ern_relic_model)

add_core_check(ui_layout_and_groups ui/layout_tests.cpp "ui" ern_game_data)

add_core_check(ui_rendering ui/render_tests.cpp "ui" ern_application)

get_target_property(ui_sources ern_relics SOURCES)
list(FILTER ui_sources INCLUDE REGEX "^src/ui/")
list(TRANSFORM ui_sources PREPEND "${CMAKE_SOURCE_DIR}/")
target_sources(ern_ui_rendering_tests PRIVATE ${ui_sources})
target_link_libraries(ern_ui_rendering_tests PRIVATE imgui)

add_core_check(effect_choices ui/effect_choices_tests.cpp "ui;relics" ern_game_data)
set(choice_sources ${ui_sources})
list(FILTER choice_sources INCLUDE REGEX "/effect_choices[.]cpp$")
target_sources(ern_effect_choices_tests PRIVATE ${choice_sources})

# Native probes.
add_executable(ern_relics_rules_probe data/rules_probe.cpp)
target_link_libraries(ern_relics_rules_probe PRIVATE ern_application)
ern_configure_target(ern_relics_rules_probe)
add_executable(ern_relics_assets_probe data/assets_probe.cpp)
target_link_libraries(ern_relics_assets_probe PRIVATE ern_application)
ern_configure_target(ern_relics_assets_probe)
set_target_properties(ern_relics_rules_probe ern_relics_assets_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME asset_verification COMMAND "${Python3_EXECUTABLE}" -m tests.tooling.assets_tests
    --build-directory "$<TARGET_FILE_DIR:ern_relics_assets_probe>" -v)
set_tests_properties(asset_verification PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LABELS "tooling;data" TIMEOUT 60)

# DX12 integration checks.
target_sources(ern_relics PRIVATE integration/dll_fixture.cpp)
add_executable(ern_relics_host WIN32 integration/host.cpp)
set_target_properties(ern_relics_host PROPERTIES OUTPUT_NAME "ern-relics-host"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(ern_relics_host PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ern_relics_host PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
ern_configure_target(ern_relics_host)
target_link_libraries(ern_relics_host PRIVATE d3d12 dxgi)

add_test(NAME dx12_host COMMAND pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/verify.ps1"
    -BuildDirectory "$<TARGET_FILE_DIR:ern_relics_host>")
set_tests_properties(dx12_host PROPERTIES LABELS "integration;dx12" TIMEOUT 60)
add_test(NAME dx12_host_close COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/integration/host_close_tests.ps1"
    -BuildDirectory "$<TARGET_FILE_DIR:ern_relics_host>")
set_tests_properties(dx12_host_close PROPERTIES LABELS "integration;dx12" TIMEOUT 60)

