$ErrorActionPreference='Stop'
cmake --build native/build --config Release --target engine_test quality_typo_long release_test ime_mixed_tip ime_engine_host ime_dictionary_tool ime_settings ime_tray ime_profile_control e2e_tip_tests tip_com_smoke
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& native/build/Release/engine_test.exe 2>$null | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& native/build/Release/quality_typo_long.exe 2>$null | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& native/build/Release/release_test.exe 2>$null | Select-Object -Last 1
exit $LASTEXITCODE
