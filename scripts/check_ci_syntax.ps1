$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
foreach ($taskName in @('scripts/ci/prepare_runtime.ps1','scripts/ci/stage_test_runtime.ps1','scripts/ci/smoke_installer.ps1','scripts/ci/run_installed_e2e.ps1','scripts/ci/test_dictionary_update.ps1','scripts/update_public_dictionary.ps1','scripts/build_installer.ps1','scripts/capture_engine_demo.ps1','scripts/record_input_demo.ps1')) {
  $taskTokens = $null
  $taskErrors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $taskRoot $taskName),[ref]$taskTokens,[ref]$taskErrors)
  if ($taskErrors.Count) { throw "$taskName parse error: $taskErrors" }
}
Write-Output 'CI and installer scripts parse successfully.'
