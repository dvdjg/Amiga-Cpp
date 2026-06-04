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
		workbenchBlue = 0
		panelMaroon = 0
		nonBlue = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 220))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 160))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if ($p.R -gt 210 -and $p.G -gt 210 -and $p.B -gt 210) { $counts.white++ }

			$isWorkbenchBlue = ([Math]::Abs([int]$p.R - 0) -le 28) -and
				([Math]::Abs([int]$p.G - 85) -le 28) -and
				([Math]::Abs([int]$p.B - 170) -le 28)
			if ($isWorkbenchBlue) { $counts.workbenchBlue++ }

			$isPanelMaroon = ([Math]::Abs([int]$p.R - 119) -le 36) -and
				([Math]::Abs([int]$p.G - 0) -le 24) -and
				([Math]::Abs([int]$p.B - 34) -le 24)
			if ($isPanelMaroon) { $counts.panelMaroon++ }

			if (-not $isWorkbenchBlue) { $counts.nonBlue++ }
		}
	}

	if ($counts.white -lt 40) { throw "No se detecta suficiente contenido blanco legible en demo 010." }
	if ($counts.panelMaroon -lt 300) { throw "No se detecta panel principal esperado en demo 010." }
	if ($counts.workbenchBlue -lt 120) { throw "No se detecta margen Workbench esperado en demo 010." }
	if ($counts.nonBlue -lt 500) { throw "No se detecta variacion de escena suficiente en demo 010." }

	$runReport = Join-Path (Split-Path $Image -Parent) "run-report.json"
	if (!(Test-Path $runReport)) {
		throw "No existe run-report.json junto a la captura."
	}

	$report = Get-Content $runReport -Raw | ConvertFrom-Json
	if ($report.demo -ne "010_chip_slow_memory") {
		throw "run-report.json no corresponde a demo 010_chip_slow_memory."
	}
	$state = [int]$report.sideChannel.value.state
	$detail = [uint32]$report.sideChannel.value.detail
	if ($state -ne 3) {
		throw "runStatus.state no esta en Ready para demo 010 (state=$state)."
	}
	if ($detail -ne 0x00000010) {
		throw ("runStatus.detail inesperado para demo 010: 0x{0:x8}" -f $detail)
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		WhiteSamples = $counts.white
		PanelMaroonSamples = $counts.panelMaroon
		WorkbenchBlueSamples = $counts.workbenchBlue
		NonBlueSamples = $counts.nonBlue
		RunState = $state
		RunDetail = ("0x{0:x8}" -f $detail)
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
