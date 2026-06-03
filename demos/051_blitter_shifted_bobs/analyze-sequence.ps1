param(
	[switch]$Warp,
	[switch]$PixelAssert,
	[switch]$RequirePixelAssertOk
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$sequenceAnalyzer = Join-Path $root "tools\analyze\analyze-frame-sequence.ps1"
$pixelAssertScript = Join-Path $root "tools\analyze\assert-pixel-contract.ps1"
$pixelContract = Join-Path $PSScriptRoot "pixel-contract.json"
$sequenceDir = Join-Path $root "out\run\051_blitter_shifted_bobs\sequence"
$pixelAssertOut = Join-Path $root "out\analysis\051_blitter_shifted_bobs\pixel-assert"

function Invoke-RunCapture {
	param([switch]$UseWarp)

	$maxAttempts = 2
	for ($attempt = 1; $attempt -le $maxAttempts; ++$attempt) {
		if ($UseWarp) {
			& powershell -ExecutionPolicy Bypass -File $runScript `
				"demos\051_blitter_shifted_bobs" `
				-SettleMs 1200 `
				-SequenceFrames 6 `
				-SequenceIntervalMs 120 `
				-Warp
		} else {
			& powershell -ExecutionPolicy Bypass -File $runScript `
				"demos\051_blitter_shifted_bobs" `
				-SettleMs 1200 `
				-SequenceFrames 6 `
				-SequenceIntervalMs 120
		}
		if ($LASTEXITCODE -eq 0) {
			return
		}
		if ($attempt -lt $maxAttempts) {
			Start-Sleep -Seconds 2
		}
	}
	throw "No se pudo capturar la secuencia de 051_blitter_shifted_bobs."
}

Invoke-RunCapture -UseWarp:$Warp

& powershell -ExecutionPolicy Bypass -File $sequenceAnalyzer $sequenceDir
if ($LASTEXITCODE -ne 0) {
	throw "La secuencia de 051_blitter_shifted_bobs no cumple las comprobaciones base."
}

if ($PixelAssert -or $RequirePixelAssertOk) {
	& powershell -ExecutionPolicy Bypass -File $pixelAssertScript `
		-SequenceDir $sequenceDir `
		-Contract $pixelContract `
		-OutDir $pixelAssertOut
	if ($LASTEXITCODE -ne 0) {
		throw "Pixel Assertions detecto desviacion en 051_blitter_shifted_bobs."
	}
}
