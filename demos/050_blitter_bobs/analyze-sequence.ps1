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
$sequenceDir = Join-Path $root "out\run\050_blitter_bobs\sequence"
$pixelAssertOut = Join-Path $root "out\analysis\050_blitter_bobs\pixel-assert"

function Invoke-RunCapture {
	param([switch]$UseWarp)

	$maxAttempts = 2
	for ($attempt = 1; $attempt -le $maxAttempts; ++$attempt) {
		if ($UseWarp) {
			& powershell -ExecutionPolicy Bypass -File $runScript `
				"demos\050_blitter_bobs" `
				-SettleMs 1200 `
				-SequenceFrames 6 `
				-SequenceIntervalMs 120 `
				-Warp
		} else {
			& powershell -ExecutionPolicy Bypass -File $runScript `
				"demos\050_blitter_bobs" `
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
	throw "No se pudo capturar la secuencia de 050_blitter_bobs."
}

Invoke-RunCapture -UseWarp:$Warp

& powershell -ExecutionPolicy Bypass -File $sequenceAnalyzer $sequenceDir -ExpectStatic
if ($LASTEXITCODE -ne 0) {
	throw "La secuencia de 050_blitter_bobs no es estable tras el frame validado."
}

if ($PixelAssert -or $RequirePixelAssertOk) {
	& powershell -ExecutionPolicy Bypass -File $pixelAssertScript `
		-SequenceDir $sequenceDir `
		-Contract $pixelContract `
		-OutDir $pixelAssertOut
	if ($LASTEXITCODE -ne 0) {
		throw "Pixel Assertions detecto desviacion en 050_blitter_bobs."
	}
}
