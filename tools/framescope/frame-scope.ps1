<#
.SYNOPSIS
	Analiza secuencias de frames o videos y genera evidencia temporal compacta.

.DESCRIPTION
	FrameScope existe para que las pruebas visuales no dependan de mirar capturas
	completas manualmente. Lee una carpeta de imagenes o un video local, reduce cada
	frame a una rejilla de luminancia/color, estima desplazamientos entre frames y
	escribe un informe JSON, un resumen Markdown y una hoja de contacto.

	El modo generico solo responde preguntas de bajo nivel: si la secuencia cambia,
	en que direccion se desplaza el contenido y que zonas varian. El perfil
	`amiga-scroll` anade una segunda capa: lee `run-report.json`, decodifica la
	telemetria lateral `g_amg_run_status` y contrasta la camara programada con el
	movimiento visual observado. Ese perfil activa recorte automatico del viewport
	para que los bordes negros de WinUAE no dominen la medicion.

.EXAMPLE
	powershell -ExecutionPolicy Bypass -File .\tools\framescope\frame-scope.ps1 `
	  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
	  -Profile amiga-scroll `
	  -GridWidth 64 -GridHeight 48 -SearchRadius 8 `
	  -RequireProfileMatch -ExpectAnimated
#>

param(
	[Parameter(Mandatory = $true)]
	[string]$Source,

	[string]$OutDir = "",

	[int]$Fps = 12,

	[int]$MaxFrames = 120,

	[int]$GridWidth = 32,

	[int]$GridHeight = 24,

	[int]$SearchRadius = 4,

	[int]$DiffThreshold = 6,

	[switch]$AutoCropContent,

	[ValidateSet("generic", "amiga-scroll")]
	[string]$Profile = "generic",

	[string]$RunReport = "",

	[int]$MaxProfileMismatches = 0,

	[switch]$ExpectAnimated,

	[switch]$RequireProfileMatch,

	[switch]$KeepExtractedFrames
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function Resolve-OutputDirectory {
	param([string]$InputPath, [string]$Requested)

	if ($Requested -ne "") {
		New-Item -ItemType Directory -Force -Path $Requested | Out-Null
		return (Resolve-Path $Requested).Path
	}

	$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
	$name = [IO.Path]::GetFileNameWithoutExtension($InputPath)
	if ($name -eq "") {
		$name = Split-Path $InputPath -Leaf
	}
	$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
	$path = Join-Path $root "out\framescope\$name-$stamp"
	New-Item -ItemType Directory -Force -Path $path | Out-Null
	return $path
}

function Test-IsVideoFile {
	param([string]$Path)

	$ext = [IO.Path]::GetExtension($Path).ToLowerInvariant()
	return @(".mp4", ".mkv", ".mov", ".avi", ".webm", ".mpg", ".mpeg", ".m4v") -contains $ext
}

function Get-ImageFiles {
	param([string]$Path)

	$extensions = @(".png", ".jpg", ".jpeg", ".bmp")
	$allFiles = @(Get-ChildItem -Path $Path -File | Where-Object {
		$extensions -contains $_.Extension.ToLowerInvariant()
	} | Sort-Object Name)
	$frameNamed = @($allFiles | Where-Object { $_.BaseName -like "frame_*" })
	$files = if ($frameNamed.Count -gt 0) { $frameNamed } else { $allFiles }
	return @($files)
}

function Expand-VideoFrames {
	param(
		[string]$VideoPath,
		[string]$OutputDir,
		[int]$FramesPerSecond,
		[int]$Limit
	)

	$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
	if ($null -eq $ffmpeg) {
		throw "FrameScope necesita ffmpeg para leer video. Instala ffmpeg o pasa una carpeta de frames PNG/JPG."
	}

	$framesDir = Join-Path $OutputDir "extracted-frames"
	New-Item -ItemType Directory -Force -Path $framesDir | Out-Null
	$pattern = Join-Path $framesDir "frame_%05d.png"
	$args = @(
		"-y",
		"-i", $VideoPath,
		"-vf", "fps=$FramesPerSecond",
		"-frames:v", $Limit,
		$pattern
	)
	& $ffmpeg.Source @args | Out-Null
	if ($LASTEXITCODE -ne 0) {
		throw "ffmpeg no pudo extraer frames de $VideoPath."
	}
	return $framesDir
}

function Get-Luma {
	param([System.Drawing.Color]$Pixel)
	return (0.299 * $Pixel.R) + (0.587 * $Pixel.G) + (0.114 * $Pixel.B)
}

function Get-ColorSymbol {
	param([System.Drawing.Color]$Pixel)

	$luma = Get-Luma $Pixel
	if ($luma -lt 24) { return " " }
	if ($luma -gt 230) { return "W" }
	if ($Pixel.R -gt 180 -and $Pixel.G -gt 160 -and $Pixel.B -lt 120) { return "Y" }
	if ($Pixel.R -gt 180 -and $Pixel.G -gt 80 -and $Pixel.B -lt 100) { return "O" }
	if ($Pixel.R -gt 170 -and $Pixel.G -lt 120 -and $Pixel.B -lt 120) { return "R" }
	if ($Pixel.G -gt 150 -and $Pixel.R -lt 140 -and $Pixel.B -lt 140) { return "G" }
	if ($Pixel.B -gt 150 -and $Pixel.R -lt 140 -and $Pixel.G -lt 170) { return "B" }
	if ($Pixel.G -gt 140 -and $Pixel.B -gt 140 -and $Pixel.R -lt 140) { return "C" }
	if ($Pixel.R -gt 140 -and $Pixel.B -gt 140 -and $Pixel.G -lt 140) { return "M" }
	return "."
}

function Get-ContentBounds {
	param(
		[string]$Path,
		[int]$Threshold = 8
	)

	# Las capturas de WinUAE suelen incluir bordes negros grandes alrededor del
	# display Amiga. Si esos bordes entran en la rejilla temporal, el estimador de
	# movimiento puede decidir que "no se mueve nada" aunque el viewport si haga
	# scroll. Este recorte busca el rectangulo comun de contenido usando el primer
	# frame como referencia; el resto de frames se mide con las mismas coordenadas.
	$bitmap = [System.Drawing.Bitmap]::FromFile($Path)
	try {
		$left = $bitmap.Width
		$right = -1
		$top = $bitmap.Height
		$bottom = -1
		for ($y = 0; $y -lt $bitmap.Height; ++$y) {
			for ($x = 0; $x -lt $bitmap.Width; ++$x) {
				if ((Get-Luma $bitmap.GetPixel($x, $y)) -gt $Threshold) {
					if ($x -lt $left) { $left = $x }
					if ($x -gt $right) { $right = $x }
					if ($y -lt $top) { $top = $y }
					if ($y -gt $bottom) { $bottom = $y }
				}
			}
		}

		if ($right -lt $left -or $bottom -lt $top) {
			return [pscustomobject]@{
				Left = 0
				Top = 0
				Width = $bitmap.Width
				Height = $bitmap.Height
			}
		}

		return [pscustomobject]@{
			Left = $left
			Top = $top
			Width = $right - $left + 1
			Height = $bottom - $top + 1
		}
	}
	finally {
		$bitmap.Dispose()
	}
}

function Get-FrameMetrics {
	param(
		[string]$Path,
		[int]$Index,
		[int]$GridW,
		[int]$GridH,
		[object]$CropBounds = $null
	)

	$bitmap = [System.Drawing.Bitmap]::FromFile($Path)
	try {
		$sampleLeft = 0
		$sampleTop = 0
		$sampleWidth = $bitmap.Width
		$sampleHeight = $bitmap.Height
		if ($null -ne $CropBounds) {
			$sampleLeft = [Math]::Max(0, [int]$CropBounds.Left)
			$sampleTop = [Math]::Max(0, [int]$CropBounds.Top)
			$sampleWidth = [Math]::Min($bitmap.Width - $sampleLeft, [int]$CropBounds.Width)
			$sampleHeight = [Math]::Min($bitmap.Height - $sampleTop, [int]$CropBounds.Height)
		}

		$luma = New-Object 'double[,]' $GridH, $GridW
		$symbols = New-Object string[] $GridH
		$totalR = 0.0
		$totalG = 0.0
		$totalB = 0.0
		$totalLuma = 0.0
		$hashBuilder = New-Object System.Text.StringBuilder

		for ($gy = 0; $gy -lt $GridH; ++$gy) {
			$row = New-Object System.Text.StringBuilder
			for ($gx = 0; $gx -lt $GridW; ++$gx) {
				$x = [Math]::Min($bitmap.Width - 1, $sampleLeft + [Math]::Floor(($gx + 0.5) * $sampleWidth / $GridW))
				$y = [Math]::Min($bitmap.Height - 1, $sampleTop + [Math]::Floor(($gy + 0.5) * $sampleHeight / $GridH))
				$p = $bitmap.GetPixel($x, $y)
				$lum = Get-Luma $p
				$luma.SetValue($lum, $gy, $gx)
				$totalR += $p.R
				$totalG += $p.G
				$totalB += $p.B
				$totalLuma += $lum
				[void]$row.Append((Get-ColorSymbol $p))
			}
			$symbols[$gy] = $row.ToString()
		}

		$count = [Math]::Max(1, $GridW * $GridH)
		$meanLuma = $totalLuma / $count
		for ($gy = 0; $gy -lt $GridH; ++$gy) {
			for ($gx = 0; $gx -lt $GridW; ++$gx) {
				[void]$hashBuilder.Append($(if ([double]$luma.GetValue($gy, $gx) -ge $meanLuma) { "1" } else { "0" }))
			}
		}

		return [pscustomobject]@{
			Index = $Index
			Path = (Resolve-Path $Path).Path
			File = [IO.Path]::GetFileName($Path)
			Width = $bitmap.Width
			Height = $bitmap.Height
			SampleLeft = $sampleLeft
			SampleTop = $sampleTop
			SampleWidth = $sampleWidth
			SampleHeight = $sampleHeight
			MeanR = [Math]::Round($totalR / $count, 3)
			MeanG = [Math]::Round($totalG / $count, 3)
			MeanB = [Math]::Round($totalB / $count, 3)
			MeanLuma = [Math]::Round($meanLuma, 3)
			LumaGrid = $luma
			SymbolGrid = $symbols
			Signature = $hashBuilder.ToString()
		}
	}
	finally {
		$bitmap.Dispose()
	}
}

function Measure-ShiftError {
	param(
		[double[,]]$A,
		[double[,]]$B,
		[int]$GridW,
		[int]$GridH,
		[int]$Dx,
		[int]$Dy
	)

	$total = 0.0
	$count = 0
	for ($y = 0; $y -lt $GridH; ++$y) {
		$by = $y + $Dy
		if ($by -lt 0 -or $by -ge $GridH) { continue }
		for ($x = 0; $x -lt $GridW; ++$x) {
			$bx = $x + $Dx
			if ($bx -lt 0 -or $bx -ge $GridW) { continue }
			$total += [Math]::Abs(([double]$A.GetValue($y, $x)) - ([double]$B.GetValue($by, $bx)))
			++$count
		}
	}

	if ($count -eq 0) { return [double]::PositiveInfinity }
	return $total / $count
}

function Get-DirectionName {
	param([int]$Dx, [int]$Dy)

	if ($Dx -eq 0 -and $Dy -eq 0) { return "static" }
	$horizontal = if ($Dx -gt 0) { "right" } elseif ($Dx -lt 0) { "left" } else { "" }
	$vertical = if ($Dy -gt 0) { "down" } elseif ($Dy -lt 0) { "up" } else { "" }
	if ($horizontal -ne "" -and $vertical -ne "") { return "$vertical-$horizontal" }
	if ($horizontal -ne "") { return $horizontal }
	return $vertical
}

function Compare-FrameMetrics {
	param(
		[object]$A,
		[object]$B,
		[int]$GridW,
		[int]$GridH,
		[int]$Radius,
		[int]$Threshold
	)

	$totalDiff = 0.0
	$changed = 0
	$quadrants = @{ topLeft = 0.0; topRight = 0.0; bottomLeft = 0.0; bottomRight = 0.0 }
	$quadrantCounts = @{ topLeft = 0; topRight = 0; bottomLeft = 0; bottomRight = 0 }
	for ($y = 0; $y -lt $GridH; ++$y) {
		for ($x = 0; $x -lt $GridW; ++$x) {
			$diff = [Math]::Abs(([double]$A.LumaGrid.GetValue($y, $x)) - ([double]$B.LumaGrid.GetValue($y, $x)))
			$totalDiff += $diff
			if ($diff -gt $Threshold) { ++$changed }
			$key = if ($y -lt ($GridH / 2)) {
				if ($x -lt ($GridW / 2)) { "topLeft" } else { "topRight" }
			} else {
				if ($x -lt ($GridW / 2)) { "bottomLeft" } else { "bottomRight" }
			}
			$quadrants[$key] += $diff
			$quadrantCounts[$key] += 1
		}
	}

	$bestDx = 0
	$bestDy = 0
	$bestError = [double]::PositiveInfinity
	$shiftCandidates = @()
	for ($dy = -$Radius; $dy -le $Radius; ++$dy) {
		for ($dx = -$Radius; $dx -le $Radius; ++$dx) {
			$error = Measure-ShiftError -A $A.LumaGrid -B $B.LumaGrid -GridW $GridW -GridH $GridH -Dx $dx -Dy $dy
			$shiftCandidates += [pscustomobject]@{
				Dx = $dx
				Dy = $dy
				Error = $error
				Direction = Get-DirectionName -Dx $dx -Dy $dy
			}
			if ($error -lt $bestError) {
				$bestError = $error
				$bestDx = $dx
				$bestDy = $dy
			}
		}
	}

	# En tilemaps con patrones repetidos puede haber varios desplazamientos casi
	# empatados. Guardar solo el mejor produce falsos negativos: un candidato
	# "static" puede ganar por decimas aunque otro candidato siga la direccion de
	# camara. Conservamos direcciones cercanas al minimo para que los perfiles
	# semanticos puedan distinguir un fallo real de un empate visual.
	$nearLimit = [Math]::Max($bestError + 3.0, $bestError * 1.06)
	$nearDirections = @($shiftCandidates |
		Where-Object { $_.Error -le $nearLimit } |
		Sort-Object Error |
		Select-Object -ExpandProperty Direction -Unique |
		Select-Object -First 8)

	$samples = [Math]::Max(1, $GridW * $GridH)
	return [pscustomobject]@{
		From = $A.Index
		To = $B.Index
		MeanDiff = [Math]::Round($totalDiff / $samples, 3)
		ChangedCells = $changed
		ChangedRatio = [Math]::Round($changed / $samples, 4)
		ContentShiftDx = $bestDx
		ContentShiftDy = $bestDy
		ContentDirection = Get-DirectionName -Dx $bestDx -Dy $bestDy
		CandidateDirections = $nearDirections
		ShiftError = [Math]::Round($bestError, 3)
		TopLeftDiff = [Math]::Round($quadrants.topLeft / [Math]::Max(1, $quadrantCounts.topLeft), 3)
		TopRightDiff = [Math]::Round($quadrants.topRight / [Math]::Max(1, $quadrantCounts.topRight), 3)
		BottomLeftDiff = [Math]::Round($quadrants.bottomLeft / [Math]::Max(1, $quadrantCounts.bottomLeft), 3)
		BottomRightDiff = [Math]::Round($quadrants.bottomRight / [Math]::Max(1, $quadrantCounts.bottomRight), 3)
	}
}

function Get-MotionSegments {
	param([object[]]$Pairs)

	$segments = @()
	if ($Pairs.Count -eq 0) { return $segments }
	$currentDirection = $Pairs[0].ContentDirection
	$start = $Pairs[0].From
	$diffTotal = 0.0
	$count = 0
	foreach ($pair in $Pairs) {
		if ($pair.ContentDirection -ne $currentDirection) {
			$segments += [pscustomobject]@{
				StartFrame = $start
				EndFrame = $pair.From
				Direction = $currentDirection
				Frames = $count
				MeanDiff = [Math]::Round($diffTotal / [Math]::Max(1, $count), 3)
			}
			$currentDirection = $pair.ContentDirection
			$start = $pair.From
			$diffTotal = 0.0
			$count = 0
		}
		$diffTotal += $pair.MeanDiff
		++$count
	}
	$segments += [pscustomobject]@{
		StartFrame = $start
		EndFrame = $Pairs[-1].To
		Direction = $currentDirection
		Frames = $count
		MeanDiff = [Math]::Round($diffTotal / [Math]::Max(1, $count), 3)
	}
	return $segments
}

function Convert-AmigaRunStatus {
	param([object]$RunStatus)

	if ($null -eq $RunStatus -or -not $RunStatus.ok) {
		return $null
	}
	$detail = [uint32]$RunStatus.detail
	return [pscustomobject]@{
		Ok = $true
		Frame = [int]$RunStatus.frame
		Detail = ("0x{0:x8}" -f $detail)
		CameraX = [int](($detail -shr 16) -band 0xff)
		CameraY = [int](($detail -shr 8) -band 0xff)
		TileJobs = [int](($detail -shr 4) -band 0x0f)
		PrefetchFlags = [int]($detail -band 0x0f)
	}
}

function Get-AmigaExpectedContentDirection {
	param([int]$DeltaX, [int]$DeltaY)

	# Convencion validada para el MVP EHB actual:
	#
	# - X usa la pareja coarse-ceil + `BPLCON1 = 16 - fine`, por lo que aumentar la
	#   camara desplaza el contenido hacia la izquierda en pantalla.
	# - Y usa punteros de bitplane: aumentar la fila inicial muestra una parte mas
	#   baja de la superficie, asi que el contenido aparente sube en pantalla.
	#
	# El analisis barato de FrameScope estima un desplazamiento global sobre una
	# rejilla de luma. En scroll diagonal, patrones repetidos o cambios de paleta
	# pueden ocultar uno de los dos ejes. Por eso este perfil compara la direccion
	# dominante de la camara, no exige que cada par de frames reproduzca una diagonal
	# perfecta. Las fases largas de la secuencia siguen mostrando todos los ejes en
	# los segmentos agregados.
	if ([Math]::Abs($DeltaX) -gt [Math]::Abs($DeltaY)) {
		$DeltaY = 0
	} elseif ([Math]::Abs($DeltaY) -gt [Math]::Abs($DeltaX)) {
		$DeltaX = 0
	}

	$horizontal = if ($DeltaX -gt 0) { "left" } elseif ($DeltaX -lt 0) { "right" } else { "" }
	$vertical = if ($DeltaY -gt 0) { "up" } elseif ($DeltaY -lt 0) { "down" } else { "" }
	if ($horizontal -ne "" -and $vertical -ne "") { return "$vertical-$horizontal" }
	if ($horizontal -ne "") { return $horizontal }
	if ($vertical -ne "") { return $vertical }
	return "static"
}

function Test-DirectionCompatible {
	param(
		[string]$Observed,
		[string]$Expected,
		[string[]]$Candidates = @()
	)

	if ($Expected -eq "static") {
		return $Observed -eq "static"
	}
	$observedAndCandidates = @($Observed) + @($Candidates)
	foreach ($direction in $observedAndCandidates) {
		$parts = $Expected -split "-"
		$compatible = $true
		foreach ($part in $parts) {
			if ($direction -notlike "*$part*") {
				$compatible = $false
				break
			}
		}
		if ($compatible) {
			return $true
		}
	}
	return $false
}

function Resolve-RunReportPath {
	param([string]$Requested, [string]$SequencePath)

	if ($Requested -ne "") {
		return (Resolve-Path $Requested).Path
	}
	$candidate = Join-Path (Split-Path $SequencePath -Parent) "run-report.json"
	if (Test-Path $candidate) {
		return (Resolve-Path $candidate).Path
	}
	return ""
}

function Get-AmigaScrollProfile {
	param(
		[string]$ReportPath,
		[object[]]$Pairs,
		[int]$AllowedMismatches
	)

	if ($ReportPath -eq "") {
		return [pscustomobject]@{
			Status = "missing_run_report"
			Observations = @()
			Mismatches = @()
			Message = "No se encontro run-report.json para correlacionar telemetria Amiga."
		}
	}

	$runReport = Get-Content $ReportPath -Raw | ConvertFrom-Json
	$telemetry = @()
	if ($runReport.sequence -and $runReport.sequence.frames) {
		for ($i = 0; $i -lt $runReport.sequence.frames.Count; ++$i) {
			$decoded = Convert-AmigaRunStatus -RunStatus $runReport.sequence.frames[$i].runStatus
			if ($null -ne $decoded) {
				$decoded | Add-Member -NotePropertyName SequenceIndex -NotePropertyValue $i
				$telemetry += $decoded
			}
		}
	}

	$observations = @()
	$mismatches = @()
	for ($i = 1; $i -lt $telemetry.Count -and ($i - 1) -lt $Pairs.Count; ++$i) {
		$prev = $telemetry[$i - 1]
		$cur = $telemetry[$i]
		$pair = $Pairs[$i - 1]
		$dx = $cur.CameraX - $prev.CameraX
		$dy = $cur.CameraY - $prev.CameraY
		$expected = Get-AmigaExpectedContentDirection -DeltaX $dx -DeltaY $dy
		$compatible = Test-DirectionCompatible -Observed $pair.ContentDirection -Expected $expected -Candidates $pair.CandidateDirections
		$observation = [pscustomobject]@{
			From = $pair.From
			To = $pair.To
			ProgramFrameFrom = $prev.Frame
			ProgramFrameTo = $cur.Frame
			CameraFrom = "$($prev.CameraX),$($prev.CameraY)"
			CameraTo = "$($cur.CameraX),$($cur.CameraY)"
			CameraDeltaX = $dx
			CameraDeltaY = $dy
			ExpectedContentDirection = $expected
			ObservedContentDirection = $pair.ContentDirection
			CandidateDirections = $pair.CandidateDirections
			ObservedShift = "$($pair.ContentShiftDx),$($pair.ContentShiftDy)"
			MeanDiff = $pair.MeanDiff
			Compatible = $compatible
		}
		$observations += $observation
		if (-not $compatible) {
			$mismatches += $observation
		}
	}

	$status = if ($telemetry.Count -lt 2) {
		"insufficient_telemetry"
	} elseif ($mismatches.Count -gt $AllowedMismatches) {
		"mismatch"
	} elseif ($mismatches.Count -gt 0) {
		"ok_with_tolerance"
	} else {
		"ok"
	}

	return [pscustomobject]@{
		Status = $status
		RunReport = $ReportPath
		TelemetryFrames = $telemetry.Count
		AllowedMismatches = $AllowedMismatches
		Observations = $observations
		Mismatches = $mismatches
	}
}

function New-ContactSheet {
	param(
		[object[]]$FrameFiles,
		[object[]]$Pairs,
		[string]$Path
	)

	$thumbW = 192
	$thumbH = 154
	$columns = [Math]::Min(4, [Math]::Max(1, $FrameFiles.Count))
	$rows = [Math]::Ceiling($FrameFiles.Count / $columns)
	$sheet = New-Object System.Drawing.Bitmap ($thumbW * $columns), ($thumbH * $rows)
	$graphics = [System.Drawing.Graphics]::FromImage($sheet)
	try {
		$graphics.Clear([System.Drawing.Color]::FromArgb(20, 20, 20))
		$font = New-Object System.Drawing.Font "Consolas", 8
		$brush = [System.Drawing.Brushes]::White
		for ($i = 0; $i -lt $FrameFiles.Count; ++$i) {
			$src = [System.Drawing.Bitmap]::FromFile($FrameFiles[$i].FullName)
			try {
				$col = $i % $columns
				$row = [Math]::Floor($i / $columns)
				$x = $col * $thumbW
				$y = $row * $thumbH
				$graphics.DrawImage($src, $x, $y + 26, $thumbW, $thumbH - 26)
				$label = "f$i $($FrameFiles[$i].Name)"
				if ($i -gt 0) {
					$pair = $Pairs[$i - 1]
					$label = "f$i d=$($pair.MeanDiff) $($pair.ContentDirection) ($($pair.ContentShiftDx),$($pair.ContentShiftDy))"
				}
				$graphics.DrawString($label, $font, $brush, $x + 4, $y + 4)
			}
			finally {
				$src.Dispose()
			}
		}
		$sheet.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally {
		$graphics.Dispose()
		$sheet.Dispose()
	}
}

$resolvedInput = (Resolve-Path $Source).Path
$outputDir = Resolve-OutputDirectory -InputPath $resolvedInput -Requested $OutDir
$temporaryFrames = $false

if ((Test-Path $resolvedInput -PathType Leaf) -and (Test-IsVideoFile $resolvedInput)) {
	$sequenceDir = Expand-VideoFrames -VideoPath $resolvedInput -OutputDir $outputDir -FramesPerSecond $Fps -Limit $MaxFrames
	$temporaryFrames = -not $KeepExtractedFrames
} elseif (Test-Path $resolvedInput -PathType Container) {
	$sequenceDir = $resolvedInput
} else {
	throw "FrameScope espera una carpeta de frames o un video soportado: $Source"
}

$frameFiles = Get-ImageFiles -Path $sequenceDir
if ($frameFiles.Count -eq 0) {
	throw "No se encontraron frames PNG/JPG/BMP en $sequenceDir."
}
if ($frameFiles.Count -gt $MaxFrames) {
	$frameFiles = @($frameFiles | Select-Object -First $MaxFrames)
}

$cropBounds = $null
if ($AutoCropContent -or $Profile -eq "amiga-scroll") {
	$cropBounds = Get-ContentBounds -Path $frameFiles[0].FullName
}

$metrics = @()
for ($i = 0; $i -lt $frameFiles.Count; ++$i) {
	$metrics += Get-FrameMetrics -Path $frameFiles[$i].FullName -Index $i -GridW $GridWidth -GridH $GridHeight -CropBounds $cropBounds
}

$pairs = @()
for ($i = 1; $i -lt $metrics.Count; ++$i) {
	$pairs += Compare-FrameMetrics -A $metrics[$i - 1] -B $metrics[$i] -GridW $GridWidth -GridH $GridHeight -Radius $SearchRadius -Threshold $DiffThreshold
}

$segments = @(Get-MotionSegments -Pairs $pairs)
$changedPairs = @($pairs | Where-Object { $_.ChangedCells -gt 0 })
$status = "ok"
if ($ExpectAnimated -and $changedPairs.Count -eq 0) {
	$status = "expected_animation_but_sequence_is_static"
}

$profileReport = $null
if ($Profile -eq "amiga-scroll") {
	$runReportPath = Resolve-RunReportPath -Requested $RunReport -SequencePath $sequenceDir
	$profileReport = Get-AmigaScrollProfile -ReportPath $runReportPath -Pairs $pairs -AllowedMismatches $MaxProfileMismatches
	if ($RequireProfileMatch -and $profileReport.Status -notin @("ok", "ok_with_tolerance")) {
		$status = "profile_$($profileReport.Status)"
	}
}

$contactSheet = Join-Path $outputDir "framescope-contact-sheet.png"
New-ContactSheet -FrameFiles $frameFiles -Pairs $pairs -Path $contactSheet

$compactFrames = @()
foreach ($metric in $metrics) {
	$compactFrames += [pscustomobject]@{
		Index = $metric.Index
		File = $metric.File
		Width = $metric.Width
		Height = $metric.Height
		SampleLeft = $metric.SampleLeft
		SampleTop = $metric.SampleTop
		SampleWidth = $metric.SampleWidth
		SampleHeight = $metric.SampleHeight
		MeanR = $metric.MeanR
		MeanG = $metric.MeanG
		MeanB = $metric.MeanB
		MeanLuma = $metric.MeanLuma
		SymbolGrid = $metric.SymbolGrid
		Signature = $metric.Signature
	}
}

$summaryText = New-Object System.Text.StringBuilder
[void]$summaryText.AppendLine("# FrameScope summary")
[void]$summaryText.AppendLine("")
[void]$summaryText.AppendLine("Input: $resolvedInput")
[void]$summaryText.AppendLine("Frames: $($metrics.Count)")
[void]$summaryText.AppendLine("Status: $status")
[void]$summaryText.AppendLine("ContactSheet: $contactSheet")
if ($null -ne $cropBounds) {
	[void]$summaryText.AppendLine("Crop: left=$($cropBounds.Left), top=$($cropBounds.Top), width=$($cropBounds.Width), height=$($cropBounds.Height)")
}
[void]$summaryText.AppendLine("")
[void]$summaryText.AppendLine("## Motion segments")
foreach ($segment in $segments) {
	[void]$summaryText.AppendLine("- frames $($segment.StartFrame)..$($segment.EndFrame): $($segment.Direction), samples=$($segment.Frames), meanDiff=$($segment.MeanDiff)")
}
[void]$summaryText.AppendLine("")
if ($null -ne $profileReport) {
	[void]$summaryText.AppendLine("## Profile amiga-scroll")
	[void]$summaryText.AppendLine("Status: $($profileReport.Status)")
	[void]$summaryText.AppendLine("TelemetryFrames: $($profileReport.TelemetryFrames)")
	[void]$summaryText.AppendLine("AllowedMismatches: $($profileReport.AllowedMismatches)")
	if ($profileReport.RunReport) {
		[void]$summaryText.AppendLine("RunReport: $($profileReport.RunReport)")
	}
foreach ($observation in $profileReport.Observations) {
	$mark = if ($observation.Compatible) { "OK" } else { "MISMATCH" }
	$candidatesText = if ($observation.CandidateDirections) { ", candidates=$($observation.CandidateDirections -join '/')" } else { "" }
	[void]$summaryText.AppendLine("- $mark frames $($observation.From)..$($observation.To): cam $($observation.CameraFrom) -> $($observation.CameraTo), expected=$($observation.ExpectedContentDirection), observed=$($observation.ObservedContentDirection), shift=$($observation.ObservedShift), diff=$($observation.MeanDiff)$candidatesText")
}
}
[void]$summaryText.AppendLine("")
[void]$summaryText.AppendLine("## Frame grids")
foreach ($frame in $compactFrames) {
	[void]$summaryText.AppendLine("### frame $($frame.Index) $($frame.File) luma=$($frame.MeanLuma)")
	foreach ($line in $frame.SymbolGrid) {
		[void]$summaryText.AppendLine($line)
	}
	[void]$summaryText.AppendLine("")
}

$report = [pscustomobject]@{
	Status = $status
	Input = $resolvedInput
	OutputDir = $outputDir
	SequenceDir = $sequenceDir
	Frames = $metrics.Count
	GridWidth = $GridWidth
	GridHeight = $GridHeight
	SearchRadius = $SearchRadius
	DiffThreshold = $DiffThreshold
	CropBounds = $cropBounds
	ContactSheet = $contactSheet
	Profile = $Profile
	ProfileReport = $profileReport
	Segments = $segments
	Pairs = $pairs
	FrameMetrics = $compactFrames
}

$jsonPath = Join-Path $outputDir "framescope-report.json"
$summaryPath = Join-Path $outputDir "framescope-summary.md"
$report | ConvertTo-Json -Depth 12 | Set-Content -Path $jsonPath -Encoding UTF8
$summaryText.ToString() | Set-Content -Path $summaryPath -Encoding UTF8

if ($temporaryFrames -and (Test-Path $sequenceDir)) {
	Remove-Item -Path $sequenceDir -Recurse -Force
}

[pscustomobject]@{
	Status = $status
	Frames = $metrics.Count
	Segments = $segments.Count
	ContactSheet = $contactSheet
	Json = $jsonPath
	Summary = $summaryPath
} | Format-List

if ($status -ne "ok") {
	throw "FrameScope fallo: $status"
}
