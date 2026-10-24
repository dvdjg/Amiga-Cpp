param(
    [string]$Demo = "demos/techniques/amiga/sprites/225_sprite_director_stress",
    [int]$Width = 1500,
    [int]$Height = 1000,
    [string]$Out = "out/vision/hud-resize-check.png",
    [int]$MinPixels = 500,
    [int]$MinBands = 1
)

# Regresión visual del HUD de host al redimensionar la ventana de WinUAE.
# Lanza run-demo, redimensiona la ventana en segundo plano y valida con
# image_probe.py que el bloque multilínea sigue presente (varias bandas de tinta).
# Requiere WinUAE, run-demo y Pillow. Uso:
#   powershell -File tools/vision-review/hud-resize-check.ps1
#   powershell -File ... -Demo <ruta> -Width 1600 -Height 1000

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$outPath = [IO.Path]::GetFullPath((Join-Path $root $Out))
$probe = Join-Path $root "tools/vision-review/image_probe.py"
$runDemo = Join-Path $root "tools/run/run-demo.sh"
$gitBash = "C:\Program Files\Git\bin\bash.exe"

$resize = Start-Job -ScriptBlock {
    param($w, $h)
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HudResize {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
    for ($i = 0; $i -lt 40; $i++) {
        $p = Get-Process | Where-Object { $_.MainWindowTitle -like "*WinUAE*" } | Select-Object -First 1
        if ($p -and $p.MainWindowHandle -ne [IntPtr]::Zero) {
            [HudResize]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 20, 20, $w, $h, 0x0040) | Out-Null
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "No apareció la ventana de WinUAE para redimensionar."
} -ArgumentList $Width, $Height

try {
    Push-Location $root
    & $gitBash "./tools/run/run-demo.sh" $Demo --warp --screenshot $Out
    $runCode = $LASTEXITCODE
    Pop-Location
    if ($runCode -ne 0) {
        throw "run-demo fallo con codigo $runCode"
    }
    if (-not (Test-Path -LiteralPath $outPath)) {
        throw "No se generó la captura: $outPath"
    }
    $json = & python $probe probe $outPath --color white --region 0,0,700,120 --min-count 3 --json
    if ($LASTEXITCODE -ne 0) {
        throw "image_probe falló con código $LASTEXITCODE"
    }
    $result = $json | ConvertFrom-Json
    $bands = @($result.bands).Count
    if ($result.pixels -lt $MinPixels -or $bands -lt $MinBands) {
        throw "HUD incompleto tras resize: pixels=$($result.pixels), bands=$bands (min $MinPixels/$MinBands)"
    }
    Write-Output "HUD resize OK: pixels=$($result.pixels), bands=$bands, screenshot=$outPath"
} finally {
    Stop-Job $resize -ErrorAction SilentlyContinue | Out-Null
    Remove-Job $resize -Force -ErrorAction SilentlyContinue | Out-Null
}
