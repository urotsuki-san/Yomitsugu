param([Parameter(Mandatory=$true)][string]$Result, [switch]$Click)
$ErrorActionPreference='Stop'
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted') { throw 'GitHub-hosted desktop required' }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TrayMouse {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; public Point(int x,int y){this.x=x;this.y=y;} }
    [StructLayout(LayoutKind.Sequential)] struct Mouse { public int x,y; public uint data,flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint type; public Mouse mouse; }
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SetPhysicalCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int property);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPhysicalPoint(Point point);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr hwnd,uint flags);
    [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint n,Input[] events,int size);
    public static long RootAt(int x,int y){return GetAncestor(WindowFromPhysicalPoint(new Point(x,y)),2).ToInt64();}
    public static bool Click() {
        var inputs=new Input[2];
        inputs[0].mouse.flags=0x0002;
        inputs[1].mouse.flags=0x0004;
        return SendInput(2,inputs,Marshal.SizeOf(typeof(Input)))==2;
    }
}
'@
[void][TrayMouse]::SetThreadDpiAwarenessContext([IntPtr](-4))
$root=[Windows.Automation.AutomationElement]::RootElement
$cond=New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ClassNameProperty,'Shell_TrayWnd')
$bar=$root.FindFirst([Windows.Automation.TreeScope]::Children,$cond)
$rows=@()
$modeEntries=@()
$inputButtons=@()
if ($bar) {
    $children=$bar.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
    foreach ($child in $children) {
        try {
            $c=$child.Current
            $rect=$c.BoundingRectangle
            $row=[ordered]@{name=$c.Name;id=$c.AutomationId;type=$c.ControlType.ProgrammaticName;class=$c.ClassName;offscreen=$c.IsOffscreen}
            if (!$rect.IsEmpty) { $row.bounds=@($rect.X,$rect.Y,$rect.Width,$rect.Height) }
            $rows+=$row
            if (!$c.IsOffscreen -and $c.ControlType -eq [Windows.Automation.ControlType]::Button) {
                $inputButtons+=$child
                if ($c.Name -match 'よみつぐ.*(日本語入力|直接入力)|Yomitsugu.*(IME mode|Japanese input|Direct input)' -or $c.AutomationId -match 'JpnImeMode|ImeModeButton|IME_Mode_Button') { $modeEntries+=$child }
            }
        } catch {}
    }
}
$diagnostics=@()
$outcome='識別できる入力モードボタンがない'
$hit=$null
if ($modeEntries.Count -eq 1) {
    $target=$modeEntries[0]
    $outcome='入力モードのボタンを検出'
    if ($Click) {
        $barHwnd=[IntPtr]$bar.Current.NativeWindowHandle
        $r=$target.Current.BoundingRectangle
        $dpi=[TrayMouse]::GetDpiForWindow($barHwnd)
        $height=[TrayMouse]::GetSystemMetrics(1)
        # Hover before testing, so an auto-hidden taskbar finishes appearing.
        [void][TrayMouse]::SetPhysicalCursorPos([int]($r.X+$r.Width/3),[int]($height-1))
        Start-Sleep -Milliseconds 650
        $r=$target.Current.BoundingRectangle
        $points=@()
        try {
            Add-Type -AssemblyName System.Drawing
            $left=[Math]::Max(0,[int]$r.X-16)
            $top=[Math]::Max(0,[int]$r.Y-4)
            $width=[Math]::Min(200,[TrayMouse]::GetSystemMetrics(0)-$left)
            $crop=[Drawing.Bitmap]::new($width,[Math]::Min(60,$height-$top))
            $graphics=[Drawing.Graphics]::FromImage($crop)
            $graphics.CopyFromScreen($left,$top,0,0,$crop.Size)
            $crop.Save([IO.Path]::ChangeExtension($Result,'.png'),[Drawing.Imaging.ImageFormat]::Png)
        } catch {} finally {
            if ($graphics) {$graphics.Dispose()}
            if ($crop) {$crop.Dispose()}
        }
        try { $points+= $target.GetClickablePoint() } catch {}
        foreach ($part in @(0.33,0.5,0.22,0.68)) {
            $points+=New-Object System.Windows.Point([Math]::Floor($r.X+$r.Width*$part),[Math]::Floor($r.Y+$r.Height/2))
        }
        foreach ($p in $points) {
            if (!$r.Contains($p)) {continue}
            [void][TrayMouse]::SetPhysicalCursorPos([int]$p.X,[int]$p.Y)
            Start-Sleep -Milliseconds 90
            $nativeRoot=[TrayMouse]::RootAt([int]$p.X,[int]$p.Y)
            $chain=@()
            $matched=$false
            try {
                $element=[Windows.Automation.AutomationElement]::FromPoint($p)
                while ($element -and $chain.Count -lt 12) {
                    $c=$element.Current
                    $chain+=@{name=$c.Name;id=$c.AutomationId;class=$c.ClassName}
                    if ([Windows.Automation.Automation]::Compare($element,$target)) {$matched=$true;break}
                    $element=[Windows.Automation.TreeWalker]::RawViewWalker.GetParent($element)
                }
            } catch {}
            $overlapping=@($inputButtons|Where-Object { $_.Current.BoundingRectangle.Contains($p) })
            $onlyTarget=$overlapping.Count -eq 1 -and [Windows.Automation.Automation]::Compare($overlapping[0],$target)
            $diagnostics+=@{x=$p.X;y=$p.Y;native_root=$nativeRoot;shell_root=$barHwnd.ToInt64();dpi=$dpi;depth=$chain;exclusive=$onlyTarget}
            if ($matched) {$hit='UIA element'} elseif ($nativeRoot -eq $barHwnd.ToInt64() -and $onlyTarget) {$hit='Explorer and exclusive mode-button bounds'}
            if ($hit) {
                if ([TrayMouse]::Click()) { $outcome='入力モードボタンを左クリック' }
                break
            }
        }
        if (!$hit) {$outcome='ボタンのクリック位置を特定できない'}
    }
}
if (!$bar) {$outcome='Shell_TrayWndを取得できない'}
@{taskbar=[bool]$bar;items=$rows;mode_matches=$modeEntries.Count;outcome=$outcome;hit=$hit;attempts=$diagnostics} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Result -Encoding UTF8
Write-Host $outcome
