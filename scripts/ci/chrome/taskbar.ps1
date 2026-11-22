param([Parameter(Mandatory=$true)][string]$Result, [switch]$Click)
$ErrorActionPreference='Stop'
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted') { throw 'GitHub-hosted desktop required' }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [Windows.Automation.AutomationElement]::RootElement
$cond = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ClassNameProperty,'Shell_TrayWnd')
$bar = $root.FindFirst([Windows.Automation.TreeScope]::Children,$cond)
$rows = @()
$modeEntries = @()
$unavailable = 0
if ($bar) {
    $children = $bar.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
    foreach ($child in $children) {
        try {
            $c = $child.Current
            $row = [ordered]@{name=$c.Name;id=$c.AutomationId;type=$c.ControlType.ProgrammaticName;class=$c.ClassName;offscreen=$c.IsOffscreen}
            $rows += $row
            if (!$c.IsOffscreen -and ($c.Name -match 'よみつぐ.*(日本語入力|直接入力)|Yomitsugu.*(IME mode|Japanese input|Direct input)' -or $c.AutomationId -match 'JpnImeMode|ImeModeButton|IME_Mode_Button')) {
                $modeEntries += $child
            }
        } catch { $unavailable++ }
    }
}
$outcome = '識別できる入力モードボタンがない'
if ($modeEntries.Count -eq 1) {
    $outcome = '入力モードのボタンを検出'
    if ($Click) {
        $rect = $modeEntries[0].Current.BoundingRectangle
        # Aim at the main button, clear of a split button's menu arrow.
        $point = New-Object System.Windows.Point([Math]::Floor($rect.X+$rect.Width/3),[Math]::Floor($rect.Y+$rect.Height/2))
        $element = [Windows.Automation.AutomationElement]::FromPoint($point)
        $matched = $false
        while ($element) {
            if ([Windows.Automation.Automation]::Compare($element,$modeEntries[0])) { $matched=$true;break }
            $element = [Windows.Automation.TreeWalker]::RawViewWalker.GetParent($element)
        }
        if ($matched) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ModeButtonClick {
    [StructLayout(LayoutKind.Sequential)] struct Mouse { public int x,y; public uint data,flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint type; public Mouse mouse; }
    [DllImport("user32.dll")] static extern bool SetPhysicalCursorPos(int x,int y);
    [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint n,Input[] events,int size);
    public static bool At(int x,int y) {
        if (!SetPhysicalCursorPos(x,y)) return false;
        var inputs=new Input[2];
        inputs[0].mouse.flags=0x0002;
        inputs[1].mouse.flags=0x0004;
        return SendInput(2,inputs,Marshal.SizeOf(typeof(Input)))==2;
    }
}
'@
            if ([ModeButtonClick]::At([int]$point.X,[int]$point.Y)) { $outcome='入力モードボタンを左クリック' }
        } else { $outcome='ボタンのクリック位置を特定できない' }
    }
}
if (!$bar) { $outcome = 'Shell_TrayWndを取得できない' }
@{taskbar=[bool]$bar;items=$rows;mode_matches=$modeEntries.Count;unavailable=$unavailable;outcome=$outcome
  button_x=$point.X;button_y=$point.Y;hit_verified=$matched} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Result -Encoding UTF8
Write-Host $outcome
