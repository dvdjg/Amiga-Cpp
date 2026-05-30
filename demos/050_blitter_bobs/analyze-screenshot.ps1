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
		backgroundBlue = 0
		backgroundDeepBlue = 0
		bobYellow = 0
		bobWhite = 0
		blobOrange = 0
		blobMagenta = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 190))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 144))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if (Test-ColorNear $p 0 34 68 28) { $counts.backgroundDeepBlue++ }
			elseif (Test-ColorNear $p 0 68 136 28) { $counts.backgroundBlue++ }
			elseif (Test-ColorNear $p 255 255 0 28) { $counts.bobYellow++ }
			elseif (Test-ColorNear $p 255 255 255 18) { $counts.bobWhite++ }
			elseif (Test-ColorNear $p 255 136 0 28) { $counts.blobOrange++ }
			elseif (Test-ColorNear $p 255 0 255 28) { $counts.blobMagenta++ }
		}
	}

	foreach ($name in @("backgroundBlue", "backgroundDeepBlue", "bobYellow", "bobWhite", "blobOrange", "blobMagenta")) {
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
	if (($detail -band 0xff000000) -ne 0x05000000) {
		throw ("runStatus.detail no contiene la marca de la demo 050: 0x{0:x8}" -f $detail)
	}
	$jobCount = $detail -band 0xff
	$noSaveJobs = ($detail -shr 8) -band 0xff
	if ($jobCount -lt 3 -or $noSaveJobs -lt 2) {
		throw ("runStatus.detail no refleja BOB + blobs no-save: 0x{0:x8}" -f $detail)
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		BackgroundBlueSamples = $counts.backgroundBlue
		BackgroundDeepBlueSamples = $counts.backgroundDeepBlue
		BobYellowSamples = $counts.bobYellow
		BobWhiteSamples = $counts.bobWhite
		BlobOrangeSamples = $counts.blobOrange
		BlobMagentaSamples = $counts.blobMagenta
		JobCount = $jobCount
		NoSaveJobs = $noSaveJobs
		RunDetail = ("0x{0:x8}" -f $detail)
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
