param(
	[switch]$Warp
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$sequenceAnalyzer = Join-Path $root "tools\analyze\analyze-frame-sequence.ps1"
$frameScope = Join-Path $root "tools\framescope\frame-scope.ps1"
$sequenceDir = Join-Path $root "out\run\101_ehb_tile_scroll_driver\sequence"
$frameScopeOut = Join-Path $root "out\framescope\101_ehb_tile_scroll_driver"

# Esta prueba es intencionadamente temporal: la captura estatica de la demo solo
# demuestra que el estado visible es valido. La secuencia exige que el scroll por
# `BPLCON1` y punteros de bitplane cambie frames consecutivos mientras el canal
# lateral sigue operativo.
if ($Warp) {
	& powershell -ExecutionPolicy Bypass -File $runScript `
		"demos\101_ehb_tile_scroll_driver" `
		-SettleMs 3500 `
		-SequenceFrames 8 `
		-SequenceIntervalMs 80 `
		-Warp
} else {
	& powershell -ExecutionPolicy Bypass -File $runScript `
		"demos\101_ehb_tile_scroll_driver" `
		-SettleMs 3500 `
		-SequenceFrames 8 `
		-SequenceIntervalMs 80
}
if ($LASTEXITCODE -ne 0) {
	throw "No se pudo capturar la secuencia animada de 101_ehb_tile_scroll_driver."
}

& powershell -ExecutionPolicy Bypass -File $sequenceAnalyzer $sequenceDir -ExpectAnimated
if ($LASTEXITCODE -ne 0) {
	throw "La secuencia de 101_ehb_tile_scroll_driver no demuestra animacion."
}

$runReport = Join-Path $root "out\run\101_ehb_tile_scroll_driver\run-report.json"
$report = Get-Content $runReport -Raw | ConvertFrom-Json
$statusValue = if ($report.finalSideChannel -and $report.finalSideChannel.ok) { $report.finalSideChannel } else { $report.sideChannel.value }
$detail = [uint32]$statusValue.detail
$cameraX = ($detail -shr 16) -band 0xff
$cameraY = ($detail -shr 8) -band 0xff
$prefetchFlags = $detail -band 0x0f
if ($report.finalSideChannel.frame -lt 160 -or $cameraX -gt 128 -or $cameraY -gt 128 -or (($prefetchFlags -band 0x3) -ne 0x3)) {
	throw ("La secuencia no alcanzo la fase circular con prefetch X/Y valido: frame={0} detail=0x{1:x8}" -f $report.finalSideChannel.frame, $detail)
}

& powershell -ExecutionPolicy Bypass -File $frameScope `
	-Source $sequenceDir `
	-OutDir $frameScopeOut `
	-Profile amiga-scroll `
	-ExpectAnimated
if ($LASTEXITCODE -ne 0) {
	throw "FrameScope no pudo generar el diagnostico amiga-scroll."
}
