param(
	[Parameter(Mandatory = $true)]
	[string]$Image
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $Image)) {
	throw "No existe la captura: $Image"
}

Add-Type -AssemblyName System.Drawing

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Image))
try {
	if ($bitmap.Width -lt 320 -or $bitmap.Height -lt 200) {
		throw "Captura demasiado pequena: $($bitmap.Width)x$($bitmap.Height)"
	}

	$counts = @{
		white = 0
		nonBlue = 0
		dark = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 220))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 160))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if ($p.R -gt 210 -and $p.G -gt 210 -and $p.B -gt 210) { $counts.white++ }
			if ($p.R -lt 24 -and $p.G -lt 24 -and $p.B -lt 24) { $counts.dark++ }

			$isWorkbenchBlue = ([Math]::Abs([int]$p.R - 0) -le 28) -and
				([Math]::Abs([int]$p.G - 85) -le 28) -and
				([Math]::Abs([int]$p.B - 170) -le 28)
			if (-not $isWorkbenchBlue) { $counts.nonBlue++ }
		}
	}

	if ($counts.white -lt 40) { throw "No se detecta suficiente contenido blanco legible en demo 000." }
	if ($counts.nonBlue -lt 400) { throw "La captura parece dominada por fondo sin contenido de demo 000." }
	if ($counts.dark -lt 20) { throw "No se detecta contraste negro suficiente en demo 000." }

	$runReport = Join-Path (Split-Path $Image -Parent) "run-report.json"
	if (!(Test-Path $runReport)) {
		throw "No existe run-report.json junto a la captura."
	}

	$report = Get-Content $runReport -Raw | ConvertFrom-Json
	if ($report.demo -ne "000_toolchain_cpp23") {
		throw "run-report.json no corresponde a demo 000_toolchain_cpp23."
	}
	$state = [int]$report.sideChannel.value.state
	if ($state -ne 3) {
		throw "runStatus.state no esta en Ready para demo 000 (state=$state)."
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		WhiteSamples = $counts.white
		NonBlueSamples = $counts.nonBlue
		DarkSamples = $counts.dark
		RunState = $state
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
