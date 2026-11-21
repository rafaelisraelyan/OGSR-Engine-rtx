# Capture a window of a running process to PNG (works for D3D windows via
# PrintWindow with PW_RENDERFULLCONTENT, falls back to client-area BitBlt).
#
# ASCII-only on purpose: Windows PowerShell 5.1 misreads UTF-8 without BOM.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

if (-not ('WindowCaptureNative' -as [type])) {
    Add-Type -Namespace '' -Name 'WindowCaptureNative' -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
public struct POINT { public int X; public int Y; }
'@
}

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$handle = $process.MainWindowHandle
if ($handle -eq [IntPtr]::Zero) { throw "process $ProcessId has no main window" }

# Capture in physical pixels even when the host uses display scaling.
$previousDpi = [WindowCaptureNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    $rect = New-Object WindowCaptureNative+RECT
    if (-not [WindowCaptureNative]::GetClientRect($handle, [ref]$rect)) { throw "GetClientRect failed" }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { throw "window client area is empty" }

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    $printed = $false
    try {
        $printed = [WindowCaptureNative]::PrintWindow($handle, $hdc, 3) # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    }
    finally {
        $graphics.ReleaseHdc($hdc)
    }

    if (-not $printed) {
        # Fallback: copy the screen region under the window.
        $point = New-Object WindowCaptureNative+POINT
        [WindowCaptureNative]::ClientToScreen($handle, [ref]$point) | Out-Null
        $graphics2 = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics2.CopyFromScreen($point.X, $point.Y, 0, 0,
            (New-Object System.Drawing.Size($width, $height)))
        $graphics2.Dispose()
    }

    $directory = Split-Path -Parent $OutputPath
    if ($directory -ne '' -and !(Test-Path $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Write-Output "captured $width x $height -> $OutputPath"
}
finally {
    if ($previousDpi -ne [IntPtr]::Zero) {
        [WindowCaptureNative]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null
    }
}
