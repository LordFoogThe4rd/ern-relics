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

# Game checks.
add_core_check(signatures game/pattern_tests.cpp "core;game" ern_game_memory)
add_core_check(relic_reader game/reader_tests.cpp "core;game;memory" ern_game_memory)
add_core_check(relic_writer game/writer_tests.cpp "core;game;memory" ern_game_memory)
add_core_check(relic_equip game/equip_tests.cpp "core;game;memory" ern_game_memory)

# Relic checks.
add_core_check(relic_validation relics/validation_tests.cpp "core;relics;rules" ern_relic_model)
add_core_check(presets relics/preset_tests.cpp "core;relics;presets" ern_game_memory)
add_core_check(drafts relics/draft_tests.cpp "core;relics;drafts" ern_game_memory)
add_core_check(deep_relics relics/deep_tests.cpp "app;relics;rules;memory" ern_application)

# Data checks.
add_core_check(fingerprints data/fingerprint_tests.cpp "core;data" ern_game_data)
add_core_check(active_regulation data/regulation_tests.cpp "core;data;rules" ern_application)
add_core_check(preset_store data/preset_store_tests.cpp "core;data;presets" ern_game_data)
add_core_check(rotating_file data/rotating_file_tests.cpp "core;data" ern_game_data)
add_core_check(edit_journal data/edit_journal_tests.cpp "core;data" ern_game_data)

# Application checks.
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

# UI checks.
add_core_check(ui_layout_and_groups ui/layout_tests.cpp "ui" ern_game_data)
add_core_check(ui_rendering ui/render_tests.cpp "ui" ern_application)
target_link_libraries(ern_ui_rendering_tests PRIVATE ern_ui)

add_core_check(effect_choices ui/effect_choices_tests.cpp "ui;relics" ern_game_data)
target_link_libraries(ern_effect_choices_tests PRIVATE ern_ui)

# Build checks.
add_test(NAME compilation COMMAND "${Python3_EXECUTABLE}" -m tools.checks.compilation
    --build-directory "${CMAKE_BINARY_DIR}")
set_tests_properties(compilation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "build" TIMEOUT 30)
add_test(NAME layout COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/checks/layout.py")
set_tests_properties(layout PROPERTIES LABELS "build" TIMEOUT 30)
add_test(NAME layout_rejection COMMAND "${Python3_EXECUTABLE}" -m tests.integration.layout_tests)
set_tests_properties(layout_rejection PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "build" TIMEOUT 30)
add_test(NAME ci_changes COMMAND "${Python3_EXECUTABLE}" -m tests.tooling.ci_changes_tests)
set_tests_properties(ci_changes PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "build" TIMEOUT 30)
add_test(NAME verify_paths COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/integration/verify_paths_tests.ps1")
set_tests_properties(verify_paths PROPERTIES LABELS "build" TIMEOUT 30)

# Tooling checks.
add_test(NAME source_assets COMMAND pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/checks/source-assets.ps1" -Root "${CMAKE_SOURCE_DIR}")
add_test(NAME source_asset_rejection COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tooling/source_assets_tests.ps1")
add_test(NAME bootstrap COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tooling/bootstrap_tests.ps1")
add_test(NAME build_output COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tooling/build_output_tests.ps1")
add_test(NAME packaging COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tooling/package_tests.ps1")
add_test(NAME launch_paths COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tooling/launch_paths_tests.ps1")
add_test(NAME third_party_notices COMMAND "${Python3_EXECUTABLE}" -m tools.checks.notices)
add_test(NAME notice_rejection COMMAND "${Python3_EXECUTABLE}" -m tests.tooling.notices_tests)
set_tests_properties(third_party_notices notice_rejection PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(source_assets source_asset_rejection bootstrap build_output packaging launch_paths third_party_notices notice_rejection PROPERTIES LABELS "tooling" TIMEOUT 180)
add_test(NAME refresh_verification COMMAND "${Python3_EXECUTABLE}" -m tests.tooling.refresh_tests)
set_tests_properties(refresh_verification PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "tooling;game" TIMEOUT 30)

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
add_library(ern_relics_fixture SHARED integration/dll_fixture.cpp)
target_link_libraries(ern_relics_fixture PRIVATE ern_runtime)
ern_configure_target(ern_relics_fixture)
set_target_properties(ern_relics_fixture PROPERTIES OUTPUT_NAME "ern-relics"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixture"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
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

# Optional AddressSanitizer check.
if(ERN_ENABLE_ASAN)
    add_executable(ern_asan_probe tooling/asan_probe.cpp)
    set_target_properties(ern_asan_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    ern_configure_target(ern_asan_probe)
    add_test(NAME asan_detection COMMAND "${Python3_EXECUTABLE}" -m tests.tooling.asan_tests "$<TARGET_FILE:ern_asan_probe>")
    set_tests_properties(asan_detection PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "tooling;asan" TIMEOUT 30)
endif()
