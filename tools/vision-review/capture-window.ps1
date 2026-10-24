param(
    [int]$ProcessId = 0,
    [string]$TitleLike = "WinUAE",
    [string]$Out = "out/vision/window.png",
    [ValidateSet("window", "client", "screen")]
    [string]$Mode = "window",
    [int]$Width = 0,
    [int]$Height = 0,
    [switch]$Foreground
)

# Captura reusable de una ventana de WinUAE (o de otra app) para diagnósticos
# visuales. Evita repetir P/Invoke inline en cada investigación.
#
# - Mode=window: rectángulo completo (incluye título/bordes).
# - Mode=client: sólo el área cliente (sin título).
# - Mode=screen: la ventana tal como está en el escritorio (CopyFromScreen).
# - Width/Height > 0: redimensiona antes de capturar.

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WinCap {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
"@

if ($ProcessId -gt 0) {
    $proc = Get-Process -Id $ProcessId -ErrorAction Stop
} else {
    $proc = Get-Process | Where-Object { $_.MainWindowTitle -like "*$TitleLike*" } | Select-Object -First 1
    if (-not $proc) {
        throw "No encuentro una ventana que contenga '$TitleLike'."
    }
}
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    throw "El proceso $($proc.Id) no tiene ventana principal."
}

if ($Width -gt 0 -and $Height -gt 0) {
    [WinCap]::ShowWindow($hwnd, 9) | Out-Null
    [WinCap]::SetWindowPos($hwnd, [IntPtr]::Zero, 30, 30, $Width, $Height, 0x0040) | Out-Null
    Start-Sleep -Milliseconds 800
}
if ($Foreground) {
    [WinCap]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 300
}

$rect = New-Object WinCap+RECT
$origin = New-Object WinCap+POINT
if ($Mode -eq "client") {
    [WinCap]::GetClientRect($hwnd, [ref]$rect) | Out-Null
    [WinCap]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
    $x, $y = $origin.X, $origin.Y
    $w, $h = $rect.Right - $rect.Left, $rect.Bottom - $rect.Top
} else {
    [WinCap]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $x, $y = $rect.Left, $rect.Top
    $w, $h = $rect.Right - $rect.Left, $rect.Bottom - $rect.Top
}

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
if ($Mode -eq "window") {
    $hdc = $g.GetHdc()
    [WinCap]::PrintWindow($hwnd, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
} else {
    $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
}
$g.Dispose()
$outPath = [IO.Path]::GetFullPath($Out)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outPath)) | Out-Null
$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "$outPath $($w)x$($h) hwnd=$hwnd mode=$Mode"
