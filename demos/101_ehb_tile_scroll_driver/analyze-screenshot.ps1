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
		[int]$Tolerance = 34
	)

	return ([Math]::Abs([int]$Pixel.R - $R) -le $Tolerance) -and
		([Math]::Abs([int]$Pixel.G - $G) -le $Tolerance) -and
		([Math]::Abs([int]$Pixel.B - $B) -le $Tolerance)
}

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Image))
try {
	$counts = @{
		skyBlue = 0
		mountainPurple = 0
		jungleGreen = 0
		jungleLight = 0
		ruinGold = 0
		underWater = 0
		underCyan = 0
		whiteSpark = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 240))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 180))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)
			if (Test-ColorNear $p 0 102 238 38) { $counts.skyBlue++ }
			elseif (Test-ColorNear $p 136 102 187 38) { $counts.mountainPurple++ }
			elseif (Test-ColorNear $p 0 255 68 38) { $counts.jungleGreen++ }
			elseif (Test-ColorNear $p 68 255 136 38) { $counts.jungleLight++ }
			elseif (Test-ColorNear $p 255 204 136 38) { $counts.ruinGold++ }
			elseif (Test-ColorNear $p 68 221 221 38) { $counts.underWater++ }
			elseif (Test-ColorNear $p 0 221 221 60) { $counts.underCyan++ }
			elseif (Test-ColorNear $p 255 255 255 24) { $counts.whiteSpark++ }
		}
	}

	foreach ($name in @("skyBlue", "mountainPurple", "jungleGreen", "jungleLight", "ruinGold", "whiteSpark")) {
		if ($counts[$name] -lt 12) {
			throw "No se detectan suficientes muestras $name en la captura."
		}
	}
	if (($counts.underWater + $counts.underCyan) -lt 12) {
		throw "No se detectan suficientes muestras de la zona inferior animada en la captura."
	}

	$runReport = Join-Path (Split-Path $Image -Parent) "run-report.json"
	if (!(Test-Path $runReport)) {
		throw "No existe run-report.json junto a la captura."
	}

	$report = Get-Content $runReport -Raw | ConvertFrom-Json
	$statusValue = if ($report.finalSideChannel -and $report.finalSideChannel.ok) { $report.finalSideChannel } else { $report.sideChannel.value }
	$detail = [uint32]$statusValue.detail
	if (($detail -band 0xff000000) -ne 0x11000000) {
		throw ("runStatus.detail no contiene la marca de la demo 101: 0x{0:x8}" -f $detail)
	}

	$cameraX = ($detail -shr 16) -band 0xff
	$fineX = ($detail -shr 8) -band 0x0f
	$tileUpdates = ($detail -shr 4) -band 0x0f
	$ringColumns = $detail -band 0x0f
	if ($cameraX -gt 48 -or $fineX -ne ($cameraX -band 0x0f) -or $tileUpdates -gt 2 -or $ringColumns -lt 1) {
		throw ("runStatus.detail no refleja driver tile scroll animado valido: 0x{0:x8}" -f $detail)
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		SkyBlueSamples = $counts.skyBlue
		MountainPurpleSamples = $counts.mountainPurple
		JungleGreenSamples = $counts.jungleGreen
		JungleLightSamples = $counts.jungleLight
		RuinGoldSamples = $counts.ruinGold
		UnderWaterSamples = $counts.underWater
		UnderCyanSamples = $counts.underCyan
		WhiteSparkSamples = $counts.whiteSpark
		CameraX = $cameraX
		FineX = $fineX
		TileUpdates = $tileUpdates
		RingColumns = $ringColumns
		RunDetail = ("0x{0:x8}" -f $detail)
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
