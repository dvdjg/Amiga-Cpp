param(
	[Parameter(Mandatory = $true)]
	[string]$Image
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $Image)) {
	throw "No existe la captura: $Image"
}

Add-Type -AssemblyName System.Drawing

function Test-ColorNear {
	param(
		[System.Drawing.Color]$Pixel,
		[int]$R,
		[int]$G,
		[int]$B,
		[int]$Tolerance = 28
	)

	return ([Math]::Abs([int]$Pixel.R - $R) -le $Tolerance) -and
		([Math]::Abs([int]$Pixel.G - $G) -le $Tolerance) -and
		([Math]::Abs([int]$Pixel.B - $B) -le $Tolerance)
}

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Image))
try {
	if ($bitmap.Width -lt 320 -or $bitmap.Height -lt 200) {
		throw "Captura demasiado pequena: $($bitmap.Width)x$($bitmap.Height)"
	}

	$counts = @{
		backgroundBlue = 0
		backgroundDeepBlue = 0
		bobYellow = 0
		bobWhite = 0
		bobCyan = 0
	}
	$activeLeft = $bitmap.Width
	$activeRight = 0
	$bobLeft = $bitmap.Width
	$bobRight = 0

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 190))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 144))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)
			$isActive = -not (Test-ColorNear $p 0 0 0 12)
			$isBob = $false

			if (Test-ColorNear $p 0 34 68 28) { $counts.backgroundDeepBlue++ }
			elseif (Test-ColorNear $p 0 68 136 28) { $counts.backgroundBlue++ }
			elseif (Test-ColorNear $p 255 255 0 28) { $counts.bobYellow++; $isBob = $true }
			elseif (Test-ColorNear $p 255 255 255 18) { $counts.bobWhite++; $isBob = $true }
			elseif (Test-ColorNear $p 0 255 255 28) { $counts.bobCyan++; $isBob = $true }

			if ($isActive) {
				$activeLeft = [Math]::Min($activeLeft, $x)
				$activeRight = [Math]::Max($activeRight, $x)
			}
			if ($isBob) {
				$bobLeft = [Math]::Min($bobLeft, $x)
				$bobRight = [Math]::Max($bobRight, $x)
			}
		}
	}

	foreach ($name in @("backgroundBlue", "backgroundDeepBlue", "bobYellow", "bobWhite", "bobCyan")) {
		if ($counts[$name] -lt 20) {
			throw "No se detectan suficientes muestras $name en la captura."
		}
	}
	if ($activeRight -le $activeLeft -or $bobRight -le $bobLeft) {
		throw "No se pudo calcular el bounding box activo/BOB."
	}

	$activeWidth = [double]($activeRight - $activeLeft + 1)
	$logicalBobLeft = (($bobLeft - $activeLeft) / $activeWidth) * 320.0
	if ($logicalBobLeft -lt 68.0 -or $logicalBobLeft -gt 82.0) {
		throw ("El BOB desplazado no cae cerca de X=73: x={0:n2}" -f $logicalBobLeft)
	}

	$runReport = Join-Path (Split-Path $Image -Parent) "run-report.json"
	if (!(Test-Path $runReport)) {
		throw "No existe run-report.json junto a la captura."
	}

	$report = Get-Content $runReport -Raw | ConvertFrom-Json
	$detail = [uint32]$report.sideChannel.value.detail
	if (($detail -band 0xff000000) -ne 0x05000000) {
		throw ("runStatus.detail no contiene la familia de demos 05x: 0x{0:x8}" -f $detail)
	}
	$demoId = ($detail -shr 20) -band 0x0f
	$shift = ($detail -shr 16) -band 0x0f
	$words = ($detail -shr 8) -band 0xff
	$jobs = $detail -band 0xff
	if ($demoId -ne 1 -or $shift -lt 1 -or $words -lt 3 -or $jobs -lt 1) {
		throw ("runStatus.detail no refleja BOB desplazado: 0x{0:x8}" -f $detail)
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		BackgroundBlueSamples = $counts.backgroundBlue
		BackgroundDeepBlueSamples = $counts.backgroundDeepBlue
		BobYellowSamples = $counts.bobYellow
		BobWhiteSamples = $counts.bobWhite
		BobCyanSamples = $counts.bobCyan
		LogicalBobLeft = ("{0:n2}" -f $logicalBobLeft)
		Shift = $shift
		WordsPerRow = $words
		FrameJobs = $jobs
		RunDetail = ("0x{0:x8}" -f $detail)
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
