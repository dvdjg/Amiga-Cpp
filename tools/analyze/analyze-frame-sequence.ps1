param(
	[Parameter(Mandatory = $true)]
	[string]$SequenceDir,

	[int]$MinFrames = 2,

	[switch]$ExpectAnimated,

	[switch]$ExpectStatic,

	[int]$DiffThreshold = 3,

	[string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$resolvedSequenceDir = (Resolve-Path $SequenceDir).Path
$outputDir = if ($OutDir -ne "") { $OutDir } else { $resolvedSequenceDir }
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$frames = Get-ChildItem -Path $resolvedSequenceDir -Filter "frame_*.png" | Sort-Object Name
if ($frames.Count -lt $MinFrames) {
	throw "La secuencia contiene $($frames.Count) frames, pero se esperaban al menos $MinFrames."
}

function Get-FrameFingerprint {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	$bitmap = [System.Drawing.Bitmap]::FromFile($Path)
	try {
		$hashBits = New-Object System.Text.StringBuilder
		$total = 0.0
		$samples = 0
		$gridW = 8
		$gridH = 8

		for ($gy = 0; $gy -lt $gridH; $gy++) {
			for ($gx = 0; $gx -lt $gridW; $gx++) {
				$x = [Math]::Min($bitmap.Width - 1, [Math]::Floor(($gx + 0.5) * $bitmap.Width / $gridW))
				$y = [Math]::Min($bitmap.Height - 1, [Math]::Floor(($gy + 0.5) * $bitmap.Height / $gridH))
				$pixel = $bitmap.GetPixel($x, $y)
				$luma = (0.299 * $pixel.R) + (0.587 * $pixel.G) + (0.114 * $pixel.B)
				$total += $luma
				$samples++
			}
		}

		$mean = $total / [Math]::Max(1, $samples)
		for ($gy = 0; $gy -lt $gridH; $gy++) {
			for ($gx = 0; $gx -lt $gridW; $gx++) {
				$x = [Math]::Min($bitmap.Width - 1, [Math]::Floor(($gx + 0.5) * $bitmap.Width / $gridW))
				$y = [Math]::Min($bitmap.Height - 1, [Math]::Floor(($gy + 0.5) * $bitmap.Height / $gridH))
				$pixel = $bitmap.GetPixel($x, $y)
				$luma = (0.299 * $pixel.R) + (0.587 * $pixel.G) + (0.114 * $pixel.B)
				[void]$hashBits.Append($(if ($luma -ge $mean) { "1" } else { "0" }))
			}
		}

		return [pscustomobject]@{
			Path = $Path
			Width = $bitmap.Width
			Height = $bitmap.Height
			Hash = $hashBits.ToString()
			MeanLuma = [Math]::Round($mean, 3)
		}
	}
	finally {
		$bitmap.Dispose()
	}
}

function Compare-FramePair {
	param(
		[Parameter(Mandatory = $true)]
		[string]$A,
		[Parameter(Mandatory = $true)]
		[string]$B,
		[int]$Threshold
	)

	$bitmapA = [System.Drawing.Bitmap]::FromFile($A)
	$bitmapB = [System.Drawing.Bitmap]::FromFile($B)
	try {
		$sampleW = 64
		$sampleH = 48
		$totalDiff = 0.0
		$changed = 0
		$samples = 0

		for ($sy = 0; $sy -lt $sampleH; $sy++) {
			for ($sx = 0; $sx -lt $sampleW; $sx++) {
				$xA = [Math]::Min($bitmapA.Width - 1, [Math]::Floor(($sx + 0.5) * $bitmapA.Width / $sampleW))
				$yA = [Math]::Min($bitmapA.Height - 1, [Math]::Floor(($sy + 0.5) * $bitmapA.Height / $sampleH))
				$xB = [Math]::Min($bitmapB.Width - 1, [Math]::Floor(($sx + 0.5) * $bitmapB.Width / $sampleW))
				$yB = [Math]::Min($bitmapB.Height - 1, [Math]::Floor(($sy + 0.5) * $bitmapB.Height / $sampleH))
				$pa = $bitmapA.GetPixel($xA, $yA)
				$pb = $bitmapB.GetPixel($xB, $yB)
				$diff = ([Math]::Abs($pa.R - $pb.R) + [Math]::Abs($pa.G - $pb.G) + [Math]::Abs($pa.B - $pb.B)) / 3.0
				$totalDiff += $diff
				if ($diff -gt $Threshold) {
					$changed++
				}
				$samples++
			}
		}

		return [pscustomobject]@{
			From = [IO.Path]::GetFileName($A)
			To = [IO.Path]::GetFileName($B)
			MeanDiff = [Math]::Round($totalDiff / [Math]::Max(1, $samples), 3)
			ChangedSamples = $changed
			ChangedRatio = [Math]::Round($changed / [Math]::Max(1, $samples), 4)
		}
	}
	finally {
		$bitmapA.Dispose()
		$bitmapB.Dispose()
	}
}

function New-ContactSheet {
	param(
		[Parameter(Mandatory = $true)]
		[object[]]$FrameFiles,
		[Parameter(Mandatory = $true)]
		[object[]]$Pairs,
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	$thumbW = 160
	$thumbH = 128
	$columns = [Math]::Min(4, [Math]::Max(1, $FrameFiles.Count))
	$rows = [Math]::Ceiling($FrameFiles.Count / $columns)
	$sheet = New-Object System.Drawing.Bitmap ($thumbW * $columns), ($thumbH * $rows)
	$graphics = [System.Drawing.Graphics]::FromImage($sheet)
	try {
		$graphics.Clear([System.Drawing.Color]::FromArgb(24, 24, 24))
		$font = New-Object System.Drawing.Font "Consolas", 9
		$brush = [System.Drawing.Brushes]::White
		for ($i = 0; $i -lt $FrameFiles.Count; $i++) {
			$src = [System.Drawing.Bitmap]::FromFile($FrameFiles[$i].FullName)
			try {
				$col = $i % $columns
				$row = [Math]::Floor($i / $columns)
				$x = $col * $thumbW
				$y = $row * $thumbH
				$graphics.DrawImage($src, $x, $y + 16, $thumbW, $thumbH - 16)
				$label = $FrameFiles[$i].Name
				if ($i -gt 0) {
					$pair = $Pairs[$i - 1]
					$label = "$label diff=$($pair.MeanDiff) ch=$($pair.ChangedSamples)"
				}
				$graphics.DrawString($label, $font, $brush, $x + 4, $y + 2)
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

$fingerprints = @()
foreach ($frame in $frames) {
	$fingerprints += Get-FrameFingerprint -Path $frame.FullName
}

$pairs = @()
for ($i = 1; $i -lt $frames.Count; $i++) {
	$pairs += Compare-FramePair -A $frames[$i - 1].FullName -B $frames[$i].FullName -Threshold $DiffThreshold
}

$changedPairs = @($pairs | Where-Object { $_.ChangedSamples -gt 0 })
$duplicatePairs = @($pairs | Where-Object { $_.ChangedSamples -eq 0 })
$meanDiffAvg = if ($pairs.Count -gt 0) { [Math]::Round((($pairs | Measure-Object -Property MeanDiff -Average).Average), 3) } else { 0 }
$maxDiff = if ($pairs.Count -gt 0) { [Math]::Round((($pairs | Measure-Object -Property MeanDiff -Maximum).Maximum), 3) } else { 0 }

$contactSheet = Join-Path $outputDir "contact-sheet.png"
New-ContactSheet -FrameFiles $frames -Pairs $pairs -Path $contactSheet

$status = "ok"
if ($ExpectAnimated -and $changedPairs.Count -eq 0) {
	$status = "expected_animation_but_frames_are_static"
}
if ($ExpectStatic -and $changedPairs.Count -gt 0) {
	$status = "expected_static_but_frames_changed"
}

$result = [pscustomobject]@{
	Status = $status
	Frames = $frames.Count
	DuplicatePairs = $duplicatePairs.Count
	ChangedPairs = $changedPairs.Count
	MeanDiffAvg = $meanDiffAvg
	MaxDiff = $maxDiff
	DiffThreshold = $DiffThreshold
	ContactSheet = $contactSheet
	Fingerprints = $fingerprints
	Pairs = $pairs
}

$jsonPath = Join-Path $outputDir "sequence-analysis.json"
$result | ConvertTo-Json -Depth 8 | Set-Content -Path $jsonPath -Encoding UTF8

$result | Select-Object Status, Frames, DuplicatePairs, ChangedPairs, MeanDiffAvg, MaxDiff, ContactSheet | Format-List

if ($status -ne "ok") {
	throw "La secuencia no cumple la expectativa: $status"
}
