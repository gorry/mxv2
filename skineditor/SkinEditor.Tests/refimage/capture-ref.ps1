# Re-capture the reference screenshot used by RenderCompareTests.
#
# Run this when the bundled skin (assets/skin/Default) or mxv2's drawing code
# changes, so that the porting test compares against the current real output.
#
#   powershell -ExecutionPolicy Bypass -File capture-ref.ps1
#
# NOTE (why this file has no Japanese comments): Windows PowerShell 5.1 reads a
# BOM-less .ps1 as CP932, so UTF-8 Japanese breaks the parser. Keep it ASCII.
#
# What it does:
#   1. starts <devroot>/build/win64/Release/mxv2.exe with its own scratch
#      -userdir, so the user's mxv2.ini is never touched
#   2. brings only that window to the front (another window on top would be
#      captured instead - this actually happened during development)
#   3. grabs the client area (DPI aware) and writes ref-Default.png here
#   4. kills only the process it started
#
# It refuses to touch an mxv2.exe that was already running.

param(
    [string]$Skin = "Default",
    [int]$ExpectWidth = 640,
    [int]$ExpectHeight = 480
)

$ErrorActionPreference = "Stop"
$env:LIB = ""
$env:INCLUDE = ""

$refDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# refimage -> SkinEditor.Tests -> skineditor -> <devroot>
$devRoot = (Get-Item $refDir).Parent.Parent.Parent.FullName
$exe = Join-Path $devRoot "build\win64\Release\mxv2.exe"
if (-not (Test-Path $exe)) {
    throw "mxv2.exe not found: $exe  (build it first: make TARGET=win64 BUILD=release build)"
}

# mxv2 quits when it cannot open an audio device, which kills the window before
# it can be captured. Nothing here needs sound, so force the dummy driver.
$env:SDL_AUDIODRIVER = "dummy"

$outPath = Join-Path $refDir ("ref-{0}.png" -f $Skin)
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("mxv2-refcap-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

  public static IntPtr Biggest(uint pid) {
    IntPtr best = IntPtr.Zero; long area = -1;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      RECT r; GetClientRect(h, out r);
      long a = (long)(r.R - r.L) * (r.B - r.T);
      if (a > area) { area = a; best = h; }
      return true;
    }, IntPtr.Zero);
    return best;
  }
}
"@

[Cap]::SetProcessDPIAware() | Out-Null

if (Get-Process mxv2 -ErrorAction SilentlyContinue) {
    throw "mxv2.exe is already running. Close it first (this script only manages the instance it starts)."
}

New-Item -ItemType Directory -Force $scratch | Out-Null
$proc = Start-Process -FilePath $exe -PassThru -WorkingDirectory (Split-Path -Parent $exe) `
    -ArgumentList "-skin", $Skin, "-zoom", "100", "-userdir", $scratch

try {
    $hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 200
        $hwnd = [Cap]::Biggest([uint32]$proc.Id)
        if ($hwnd -ne [IntPtr]::Zero) { break }
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw "mxv2 window did not appear" }

    # HWND_TOPMOST(-1), SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW
    [Cap]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null
    Start-Sleep -Milliseconds 800

    $cr = New-Object Cap+RECT
    [Cap]::GetClientRect($hwnd, [ref]$cr) | Out-Null
    $pt = New-Object Cap+POINT
    [Cap]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
    $cw = $cr.R - $cr.L
    $ch = $cr.B - $cr.T
    Write-Output ("client {0}x{1} at {2},{3}" -f $cw, $ch, $pt.X, $pt.Y)
    if ($cw -ne $ExpectWidth -or $ch -ne $ExpectHeight) {
        throw ("unexpected client size {0}x{1} (expected {2}x{3}); the skin size or -zoom is off" -f $cw, $ch, $ExpectWidth, $ExpectHeight)
    }

    $bmp = New-Object System.Drawing.Bitmap $cw, $ch
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object System.Drawing.Size($cw, $ch)))
    $g.Dispose()

    # Guard against grabbing an occluded window (a blank/uniform grab).
    $seen = @{}
    for ($y = 0; $y -lt $ch; $y += 17) {
        for ($x = 0; $x -lt $cw; $x += 17) { $seen[$bmp.GetPixel($x, $y).ToArgb()] = 1 }
    }
    if ($seen.Count -lt 10) {
        $bmp.Dispose()
        throw "the capture looks blank ($($seen.Count) distinct colors). Another window was probably on top."
    }

    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("saved {0}" -f $outPath)
    Write-Output "now run: dotnet test skineditor/SkinEditor.sln"
}
finally {
    if (-not $proc.HasExited) { $proc.Kill() }
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}
