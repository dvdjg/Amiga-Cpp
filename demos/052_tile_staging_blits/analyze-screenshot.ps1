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
		tileYellow = 0
		tileOrange = 0
		tileCyan = 0
		tileMagenta = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 220))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 160))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if (Test-ColorNear $p 0 34 68 28) { $counts.backgroundDeepBlue++ }
			elseif (Test-ColorNear $p 0 68 136 28) { $counts.backgroundBlue++ }
			elseif (Test-ColorNear $p 255 255 0 28) { $counts.tileYellow++ }
			elseif (Test-ColorNear $p 255 136 0 28) { $counts.tileOrange++ }
			elseif (Test-ColorNear $p 0 255 255 28) { $counts.tileCyan++ }
			elseif (Test-ColorNear $p 255 0 255 28) { $counts.tileMagenta++ }
		}
	}

	foreach ($name in @("backgroundBlue", "backgroundDeepBlue", "tileYellow", "tileOrange", "tileCyan", "tileMagenta")) {
		if ($counts[$name] -lt 24) {
			throw "No se detectan suficientes muestras $name en la captura."
		}
	}

	$runReport = Join-Path (Split-Path $Image -Parent) "run-report.json"
	if (!(Test-Path $runReport)) {
		throw "No existe run-report.json junto a la captura."
	}

	$report = Get-Content $runReport -Raw | ConvertFrom-Json
	$detail = [uint32]$report.sideChannel.value.detail
	if (($detail -band 0xfff00000) -ne 0x05200000) {
		throw ("runStatus.detail no contiene la marca de la demo 052: 0x{0:x8}" -f $detail)
	}

	$tileJobs = ($detail -shr 12) -band 0xff
	$presentJobs = ($detail -shr 8) -band 0xff
	$stripWords = $detail -band 0xff
	if ($tileJobs -lt 16 -or $presentJobs -lt 1 -or $stripWords -lt 4) {
		throw ("runStatus.detail no refleja staging de tiles: 0x{0:x8}" -f $detail)
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		BackgroundBlueSamples = $counts.backgroundBlue
		BackgroundDeepBlueSamples = $counts.backgroundDeepBlue
		TileYellowSamples = $counts.tileYellow
		TileOrangeSamples = $counts.tileOrange
		TileCyanSamples = $counts.tileCyan
		TileMagentaSamples = $counts.tileMagenta
		TileJobs = $tileJobs
		PresentJobs = $presentJobs
		StripWords = $stripWords
		RunDetail = ("0x{0:x8}" -f $detail)
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
