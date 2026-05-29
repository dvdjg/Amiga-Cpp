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
		[int]$Tolerance = 20
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
		topRed = 0
		topHalfRed = 0
		middleOrange = 0
		middleHalfOrange = 0
		bottomBlue = 0
		bottomHalfBlue = 0
		brightWhite = 0
	}

	# El monitor de WinUAE escala el display PAL dentro de una imagen 756x576. Para
	# que el analisis no dependa de bordes exactos, muestreamos toda la captura y
	# clasificamos colores por cercania RGB. Las bandas EHB son grandes, asi que los
	# contadores deben ser claramente superiores al ruido de bordes/escalado.
	$stepX = [Math]::Max(1, [int]($bitmap.Width / 160))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 128))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if (Test-ColorNear $p 255 0 0) { $counts.topRed++ }
			elseif (Test-ColorNear $p 136 0 0 24) { $counts.topHalfRed++ }
			elseif (Test-ColorNear $p 255 68 0 24) { $counts.middleOrange++ }
			elseif (Test-ColorNear $p 136 34 0 28) { $counts.middleHalfOrange++ }
			elseif (Test-ColorNear $p 0 68 255 24) { $counts.bottomBlue++ }
			elseif (Test-ColorNear $p 0 34 136 28) { $counts.bottomHalfBlue++ }
			elseif (Test-ColorNear $p 255 255 255 16) { $counts.brightWhite++ }
		}
	}

	foreach ($name in @("topRed", "topHalfRed", "middleOrange", "middleHalfOrange", "bottomBlue", "bottomHalfBlue", "brightWhite")) {
		if ($counts[$name] -lt 30) {
			throw "No se detectan suficientes muestras $name en la captura."
		}
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		TopRedSamples = $counts.topRed
		TopHalfRedSamples = $counts.topHalfRed
		MiddleOrangeSamples = $counts.middleOrange
		MiddleHalfOrangeSamples = $counts.middleHalfOrange
		BottomBlueSamples = $counts.bottomBlue
		BottomHalfBlueSamples = $counts.bottomHalfBlue
		WhiteSamples = $counts.brightWhite
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}
