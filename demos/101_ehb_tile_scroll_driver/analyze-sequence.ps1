param(
	[switch]$Warp,
	[switch]$VisionReview,
	[switch]$RequireVisionReviewOk,
	[switch]$PixelAssert,
	[switch]$RequirePixelAssertOk,
	[string]$VisionProvider = "",
	[ValidateSet("", "multi-image", "contact-sheet")]
	[string]$VisionSendMode = "multi-image"
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$sequenceAnalyzer = Join-Path $root "tools\analyze\analyze-frame-sequence.ps1"
$innerBlackAnalyzer = Join-Path $root "tools\analyze\assert-no-inner-black.ps1"
$pixelAssertScript = Join-Path $root "tools\analyze\assert-pixel-contract.ps1"
$fineScrollAnalyzer = Join-Path $PSScriptRoot "analyze-fine-scroll.ps1"
$frameScope = Join-Path $root "tools\framescope\frame-scope.ps1"
$visionReviewScript = Join-Path $root "tools\vision-review\vision-review.ps1"
$pixelContract = Join-Path $PSScriptRoot "pixel-contract.json"
$sequenceDir = Join-Path $root "out\run\101_ehb_tile_scroll_driver\sequence"
$frameScopeOut = Join-Path $root "out\framescope\101_ehb_tile_scroll_driver"
$visionReviewOut = Join-Path $root "out\vision-review\101_ehb_tile_scroll_driver"
$pixelAssertOut = Join-Path $root "out\analysis\101_ehb_tile_scroll_driver\pixel-assert"

# Esta prueba es intencionadamente temporal: la captura estatica de la demo solo
# demuestra que el estado visible es valido. La secuencia exige que el scroll por
# `BPLCON1` y punteros de bitplane cambie frames consecutivos mientras el canal
# lateral sigue operativo.
if ($Warp) {
	& powershell -ExecutionPolicy Bypass -File $runScript `
		"demos\101_ehb_tile_scroll_driver" `
		-SettleMs 3500 `
		-SequenceFrames 12 `
		-SequenceIntervalMs 120 `
		-Warp
} else {
	& powershell -ExecutionPolicy Bypass -File $runScript `
		"demos\101_ehb_tile_scroll_driver" `
		-SettleMs 3500 `
		-SequenceFrames 12 `
		-SequenceIntervalMs 120
}
if ($LASTEXITCODE -ne 0) {
	throw "No se pudo capturar la secuencia animada de 101_ehb_tile_scroll_driver."
}

& powershell -ExecutionPolicy Bypass -File $sequenceAnalyzer $sequenceDir -ExpectAnimated
if ($LASTEXITCODE -ne 0) {
	throw "La secuencia de 101_ehb_tile_scroll_driver no demuestra animacion."
}

& powershell -ExecutionPolicy Bypass -File $innerBlackAnalyzer $sequenceDir -MaxBlackRatio 0.001
if ($LASTEXITCODE -ne 0) {
	throw "La secuencia contiene artefactos negros internos, tipicos de un reinicio de Copper o puntero de bitplane a media pantalla."
}

$runReport = Join-Path $root "out\run\101_ehb_tile_scroll_driver\run-report.json"
$report = Get-Content $runReport -Raw | ConvertFrom-Json
$statusValue = if ($report.finalSideChannel -and $report.finalSideChannel.ok) { $report.finalSideChannel } else { $report.sideChannel.value }
$detail = [uint32]$statusValue.detail
$cameraX = ($detail -shr 16) -band 0xff
$cameraY = ($detail -shr 8) -band 0xff
$prefetchFlags = $detail -band 0x0f
if ($report.finalSideChannel.frame -lt 200 -or $cameraX -gt 128 -or $cameraY -gt 128 -or (($prefetchFlags -band 0x3) -ne 0x3)) {
	throw ("La secuencia no alcanzo la fase circular con prefetch X/Y valido: frame={0} detail=0x{1:x8}" -f $report.finalSideChannel.frame, $detail)
}

if ($PixelAssert -or $RequirePixelAssertOk) {
	& powershell -ExecutionPolicy Bypass -File $pixelAssertScript `
		-SequenceDir $sequenceDir `
		-Contract $pixelContract `
		-RunReport $runReport `
		-OutDir $pixelAssertOut
	if ($LASTEXITCODE -ne 0) {
		throw "Pixel Assertions detecto una desviacion de render en la secuencia."
	}
}

& powershell -ExecutionPolicy Bypass -File $frameScope `
	-Source $sequenceDir `
	-OutDir $frameScopeOut `
	-Profile amiga-scroll `
	-GridWidth 64 `
	-GridHeight 48 `
	-SearchRadius 12 `
	-MaxProfileMismatches 1 `
	-ExpectAnimated
if ($LASTEXITCODE -ne 0) {
	throw "FrameScope no pudo validar el diagnostico amiga-scroll."
}

$fineArgs = @("-ExecutionPolicy", "Bypass", "-File", $fineScrollAnalyzer)
if ($Warp) {
	$fineArgs += "-Warp"
}
& powershell @fineArgs
if ($LASTEXITCODE -ne 0) {
	throw "La transicion fine scroll 14,15,0,1 no es continua."
}

if ($VisionReview -or $RequireVisionReviewOk) {
	if ($VisionProvider -eq "") {
		$VisionProvider = Join-Path $root "tools\vision-review\providers\lmstudio.legion.json"
	}
	$frameScopeReport = Join-Path $frameScopeOut "framescope-report.json"
	$visionArgs = @(
		"-ExecutionPolicy", "Bypass",
		"-File", $visionReviewScript,
		"-Source", $sequenceDir,
		"-RunReport", $runReport,
		"-FrameScopeReport", $frameScopeReport,
		"-Profile", "amiga-scroll-transition",
		"-Provider", $VisionProvider,
		"-SendMode", $VisionSendMode,
		"-OutDir", $visionReviewOut
	)
	if ($RequireVisionReviewOk) {
		$visionArgs += "-RequireOk"
	}
	& powershell @visionArgs
	if ($LASTEXITCODE -ne 0) {
		throw "Vision Review no pudo validar la transicion amiga-scroll."
	}
}
