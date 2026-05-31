param(
	[switch]$Warp
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$sequenceAnalyzer = Join-Path $root "tools\analyze\analyze-frame-sequence.ps1"
$sequenceDir = Join-Path $root "out\run\101_ehb_tile_scroll_driver\sequence"

# Esta prueba es intencionadamente temporal: la captura estatica de la demo solo
# demuestra que el estado visible es valido. La secuencia exige que el scroll por
# `BPLCON1` y punteros de bitplane cambie frames consecutivos mientras el canal
# lateral sigue operativo.
if ($Warp) {
	& powershell -ExecutionPolicy Bypass -File $runScript `
		"demos\101_ehb_tile_scroll_driver" `
		-SequenceFrames 4 `
		-SequenceIntervalMs 80 `
		-Warp
} else {
	& powershell -ExecutionPolicy Bypass -File $runScript `
		"demos\101_ehb_tile_scroll_driver" `
		-SequenceFrames 4 `
		-SequenceIntervalMs 80
}
if ($LASTEXITCODE -ne 0) {
	throw "No se pudo capturar la secuencia animada de 101_ehb_tile_scroll_driver."
}

& powershell -ExecutionPolicy Bypass -File $sequenceAnalyzer $sequenceDir -ExpectAnimated
if ($LASTEXITCODE -ne 0) {
	throw "La secuencia de 101_ehb_tile_scroll_driver no demuestra animacion."
}
