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
		red = 0
		green = 0
		blue = 0
		yellow = 0
		cyan = 0
	}

	$stepX = [Math]::Max(1, [int]($bitmap.Width / 96))
	$stepY = [Math]::Max(1, [int]($bitmap.Height / 96))

	for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
		for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
			$p = $bitmap.GetPixel($x, $y)

			if ($p.R -gt 180 -and $p.G -lt 80 -and $p.B -lt 80) { $counts.red++ }
			elseif ($p.R -lt 80 -and $p.G -gt 180 -and $p.B -lt 80) { $counts.green++ }
			elseif ($p.R -lt 80 -and $p.G -lt 80 -and $p.B -gt 180) { $counts.blue++ }
			elseif ($p.R -gt 180 -and $p.G -gt 180 -and $p.B -lt 80) { $counts.yellow++ }
			elseif ($p.R -lt 80 -and $p.G -gt 180 -and $p.B -gt 180) { $counts.cyan++ }
		}
	}

	foreach ($name in @("red", "green", "blue", "yellow", "cyan")) {
		if ($counts[$name] -lt 50) {
			throw "No se detecta banda $name suficiente en la captura."
		}
	}

	[pscustomobject]@{
		Image = (Resolve-Path $Image).Path
		Width = $bitmap.Width
		Height = $bitmap.Height
		RedSamples = $counts.red
		GreenSamples = $counts.green
		BlueSamples = $counts.blue
		YellowSamples = $counts.yellow
		CyanSamples = $counts.cyan
		Status = "OK"
	} | Format-List
}
finally {
	$bitmap.Dispose()
}

