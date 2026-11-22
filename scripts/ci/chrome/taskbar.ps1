param([Parameter(Mandatory=$true)][string]$Result, [switch]$Click)
$ErrorActionPreference='Stop'
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted') { throw 'GitHub-hosted desktop required' }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [Windows.Automation.AutomationElement]::RootElement
$cond = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ClassNameProperty,'Shell_TrayWnd')
$bar = $root.FindFirst([Windows.Automation.TreeScope]::Children,$cond)
$rows = @()
$matches = @()
if ($bar) {
    $children = $bar.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
    foreach ($child in $children) {
        try {
            $c = $child.Current
            $row = [ordered]@{name=$c.Name;id=$c.AutomationId;type=$c.ControlType.ProgrammaticName;class=$c.ClassName;offscreen=$c.IsOffscreen}
            $rows += $row
            if (!$c.IsOffscreen -and ($c.Name -match 'よみつぐ.*(日本語入力|直接入力)|Yomitsugu.*(IME mode|Japanese input|Direct input)' -or $c.AutomationId -match 'JpnImeMode|ImeModeButton|IME_Mode_Button')) {
                $matches += $child
            }
        } catch [Windows.Automation.ElementNotAvailableException] {}
    }
}
$outcome = '識別できる入力モードボタンがない'
if ($matches.Count -eq 1) {
    $outcome = '入力モードのボタンを検出'
    if ($Click) {
        $pattern = $null
        if ($matches[0].TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)) {
            $pattern.Invoke()
            $outcome = '入力モードボタンをInvoke'
        } else { $outcome = '入力モードボタンにInvokePatternがない' }
    }
}
if (!$bar) { $outcome = 'Shell_TrayWndを取得できない' }
@{taskbar=[bool]$bar;items=$rows;mode_matches=$matches.Count;outcome=$outcome} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Result -Encoding UTF8
Write-Host $outcome
