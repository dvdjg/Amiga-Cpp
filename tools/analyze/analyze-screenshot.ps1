param(
	[Parameter(Mandatory = $true)]
	[string]$Image,

	[int]$MinWidth = 320,
	[int]$MinHeight = 200
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $Image)) {
	throw "No existe la captura: $Image"
}

Add-Type -AssemblyName System.Drawing

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Image))
try {
	if ($bitmap.Width -lt $MinWidth -or $bitmap.Height -lt $MinHeight) {
		throw "Captura demasiado pequena: $($bitmap.Width)x$($bitmap.Height)"
	}

	$green = 0
	$yellow = 0
	$white = 0
	$nonBlue = 0
	$samples = 0

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 220))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 160))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)
			$samples++

			if ($p.G -gt 160 -and $p.R -lt 80 -and $p.B -lt 180) { $green++ }
			if ($p.R -gt 180 -and $p.G -gt 180 -and $p.B -lt 120) { $yellow++ }
			if ($p.R -gt 210 -and $p.G -gt 210 -and $p.B -gt 210) { $white++ }

			$isWorkbenchBlue = ($p.B -gt 100 -and $p.G -gt 40 -and $p.G -lt 130 -and $p.R -lt 50)
			if (-not $isWorkbenchBlue) { $nonBlue++ }
		}
	}

	# The demo overlay intentionally contains green, yellow and white debug text.
	if ($green -lt 1) { throw "No se detecta texto verde esperado en la captura." }
	if ($yellow -lt 1) { throw "No se detecta texto amarillo esperado en la captura." }
	if ($white -lt 10) { throw "No se detecta suficiente texto/borde blanco esperado en la captura." }
	if ($nonBlue -lt 100) { throw "La captura parece ser solo el fondo AmigaDOS sin overlay suficiente." }

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		Samples = $samples
		GreenSamples = $green
		YellowSamples = $yellow
		WhiteSamples = $white
		NonBlueSamples = $nonBlue
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}

