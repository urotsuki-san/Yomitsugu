cmake_minimum_required(VERSION 3.20)
project(ime_mixed_native VERSION 0.2.9 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

if(NOT WIN32)
  message(FATAL_ERROR "Native tip targets Windows only")
endif()

add_library(ime_engine STATIC
  src/engine.cpp
  src/azookey_bridge.cpp
)
target_include_directories(ime_engine PUBLIC include PRIVATE third_party)
target_compile_definitions(ime_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_library(ime_preview STATIC src/engine.cpp src/azookey_stub.cpp)
target_include_directories(ime_preview PUBLIC include)
add_executable(ime_engine_host src/engine_host.cpp)
add_library(ime_dictionary STATIC src/user_dictionary.cpp src/learning_store.cpp)
target_include_directories(ime_dictionary PUBLIC include PRIVATE third_party)
target_compile_definitions(ime_dictionary PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ime_dictionary PUBLIC shell32 ole32)
add_executable(ime_dictionary_tool src/dictionary_tool.cpp)
target_link_libraries(ime_dictionary_tool PRIVATE ime_dictionary ime_preview)
add_executable(ime_settings WIN32 src/settings.cpp src/dictionary_editor.cpp)
add_library(ime_update STATIC src/app_update.cpp)
target_include_directories(ime_update PUBLIC include PRIVATE third_party)
target_compile_definitions(ime_update PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ime_update PUBLIC winhttp bcrypt shell32)
add_executable(app_update_test tests/app_update_test.cpp)
target_include_directories(app_update_test PRIVATE third_party)
target_link_libraries(app_update_test PRIVATE ime_update)
target_include_directories(ime_settings PRIVATE include)
target_link_libraries(ime_settings PRIVATE ime_dictionary ime_preview ime_update comdlg32 comctl32 shell32 ole32)
target_compile_definitions(ime_settings PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(dictionary_editor_host WIN32 tests/dictionary_editor_host.cpp src/dictionary_editor.cpp)
target_include_directories(dictionary_editor_host PRIVATE include)
target_link_libraries(dictionary_editor_host PRIVATE ime_dictionary ime_preview comctl32 shell32 ole32)
target_compile_definitions(dictionary_editor_host PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(ime_tray WIN32 src/tray.cpp)
target_link_libraries(ime_tray PRIVATE shell32 user32 gdi32)
target_compile_definitions(ime_tray PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(ime_profile_control src/profile_control.cpp)
target_link_libraries(ime_profile_control PRIVATE ole32 uuid)
target_include_directories(ime_engine_host PRIVATE third_party)
target_link_libraries(ime_engine_host PRIVATE ime_dictionary ime_engine)
target_compile_definitions(ime_engine_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_library(ime_channel STATIC src/engine_channel.cpp)
target_include_directories(ime_channel PUBLIC include PRIVATE third_party)
target_compile_definitions(ime_channel PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_executable(release_test tests/release_test.cpp)
target_link_libraries(release_test PRIVATE ime_channel ime_dictionary ime_engine)
target_compile_definitions(release_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_dependencies(release_test ime_engine_host)

add_executable(engine_test tests/engine_test.cpp)
target_link_libraries(engine_test PRIVATE ime_engine)

add_executable(debug_dump tests/debug_dump.cpp)
add_executable(debug_quality tests/debug_quality.cpp)
add_executable(dump_azo tests/dump_azo.cpp)
add_executable(bench_zenzai tests/bench_zenzai.cpp)
add_executable(quality_typo_long tests/quality_typo_long.cpp)
target_link_libraries(bench_zenzai PRIVATE ime_engine)
target_link_libraries(quality_typo_long PRIVATE ime_engine)
target_link_libraries(debug_quality PRIVATE ime_engine)
target_link_libraries(dump_azo PRIVATE ime_engine)
target_link_libraries(debug_dump PRIVATE ime_dictionary ime_engine)

add_library(ime_mixed_tip SHARED
  src/tip/dllmain.cpp
  src/tip/register.cpp
  src/tip/text_service.cpp
  src/tip/candidate_window.cpp
  src/tip/input_mode_menu.cpp
  src/tip/ime_mixed_tip.def
)
target_include_directories(ime_mixed_tip PRIVATE include src/tip)
target_link_libraries(ime_mixed_tip PRIVATE ime_preview ime_channel ole32 oleaut32 uuid advapi32 user32 gdi32 shell32)
target_compile_definitions(ime_mixed_tip PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX IME_TIP_RESOURCE)
target_sources(ime_mixed_tip PRIVATE src/version.rc)
target_sources(ime_engine_host PRIVATE src/version.rc)
target_sources(ime_dictionary_tool PRIVATE src/version.rc)
target_sources(ime_settings PRIVATE src/version.rc)
target_sources(ime_tray PRIVATE src/version.rc)

# 使用中のDLLを上書きしないよう、TIPと入力試験を版別のフォルダーへ出力する。
set(E2E_OUT ${CMAKE_BINARY_DIR}/e2e_out_v13)
set_target_properties(ime_mixed_tip PROPERTIES
  OUTPUT_NAME "ime_mixed_tip_v13"
  RUNTIME_OUTPUT_DIRECTORY ${E2E_OUT}
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${E2E_OUT}
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${E2E_OUT}
  LIBRARY_OUTPUT_DIRECTORY ${E2E_OUT}
  LIBRARY_OUTPUT_DIRECTORY_RELEASE ${E2E_OUT}
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(e2e_tip_tests tests/e2e_tip_tests.cpp)
target_link_libraries(e2e_tip_tests PRIVATE ole32 oleaut32 uuid user32)
target_include_directories(e2e_tip_tests PRIVATE include)
target_compile_definitions(e2e_tip_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(tip_com_smoke tests/tip_com_smoke.cpp)
target_link_libraries(tip_com_smoke PRIVATE ole32 uuid)
target_include_directories(tip_com_smoke PRIVATE include)
target_compile_definitions(tip_com_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(tip_com_smoke PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${E2E_OUT}
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${E2E_OUT}
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${E2E_OUT}
)
set_target_properties(e2e_tip_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${E2E_OUT}
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${E2E_OUT}
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${E2E_OUT}
)

enable_testing()
add_test(NAME app_update_test COMMAND app_update_test)
add_test(NAME engine_test COMMAND engine_test)
add_test(NAME release_test COMMAND release_test)
# デスクトップを操作するE2Eは、明示的に有効にした場合だけctestへ登録する。
option(IME_ENABLE_INTERACTIVE_E2E "Enable registered TIP desktop tests" OFF)
if(IME_ENABLE_INTERACTIVE_E2E)
  add_test(NAME e2e_tip_tests COMMAND e2e_tip_tests)
endif()

