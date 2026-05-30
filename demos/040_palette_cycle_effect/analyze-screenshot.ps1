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
		[int]$Tolerance = 24
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
		red = 0
		orange = 0
		yellow = 0
		green = 0
		cyan = 0
		blue = 0
		magenta = 0
		lowerBlue = 0
		white = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 160))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 128))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if (Test-ColorNear $p 255 0 0) { $counts.red++ }
			elseif (Test-ColorNear $p 255 136 0) { $counts.orange++ }
			elseif (Test-ColorNear $p 255 255 0) { $counts.yellow++ }
			elseif (Test-ColorNear $p 0 255 0) { $counts.green++ }
			elseif (Test-ColorNear $p 0 255 255) { $counts.cyan++ }
			elseif (Test-ColorNear $p 0 136 255 28) { $counts.blue++ }
			elseif (Test-ColorNear $p 255 0 255) { $counts.magenta++ }
			elseif (Test-ColorNear $p 0 68 255 28) { $counts.lowerBlue++ }
			elseif (Test-ColorNear $p 255 255 255 20) { $counts.white++ }
		}
	}

	foreach ($name in @("red", "orange", "yellow", "green", "cyan", "blue", "magenta", "lowerBlue", "white")) {
		if ($counts[$name] -lt 20) {
			throw "No se detectan suficientes muestras $name en la captura."
		}
	}

	$runReport = Join-Path (Split-Path $Image -Parent) "run-report.json"
	if (!(Test-Path $runReport)) {
		throw "No existe run-report.json junto a la captura."
	}

	$report = Get-Content $runReport -Raw | ConvertFrom-Json
	$detail = [uint32]$report.sideChannel.value.detail
	if (($detail -band 0xff000000) -ne 0x04000000) {
		throw ("runStatus.detail no contiene la marca de la demo 040: 0x{0:x8}" -f $detail)
	}
	$phase = ($detail -shr 16) -band 0xff
	if ($phase -eq 0) {
		throw ("runStatus.detail indica fase 0; la captura no demuestra ciclo activo: 0x{0:x8}" -f $detail)
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		RedSamples = $counts.red
		OrangeSamples = $counts.orange
		YellowSamples = $counts.yellow
		GreenSamples = $counts.green
		CyanSamples = $counts.cyan
		BlueSamples = $counts.blue
		MagentaSamples = $counts.magenta
		LowerBlueSamples = $counts.lowerBlue
		WhiteSamples = $counts.white
		CyclePhase = $phase
		RunDetail = ("0x{0:x8}" -f $detail)
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
