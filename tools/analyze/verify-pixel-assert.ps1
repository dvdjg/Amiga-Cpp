param(
	[string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$assertScript = Join-Path $PSScriptRoot "assert-pixel-contract.ps1"

if (-not (Test-Path $assertScript)) {
	throw "No existe assert-pixel-contract.ps1 en $PSScriptRoot"
}

$suiteOutDir = if ($OutDir -ne "") {
	$OutDir
} else {
	Join-Path $root "out\analysis\pixel-assert-selftest"
}

New-Item -ItemType Directory -Force -Path $suiteOutDir | Out-Null
$suiteOutDir = (Resolve-Path $suiteOutDir).Path

function New-SolidBitmap {
	param(
		[int]$Width,
		[int]$Height,
		[System.Drawing.Color]$Color,
		[string]$Path
	)

	$bmp = New-Object System.Drawing.Bitmap $Width, $Height
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	try {
		$brush = New-Object System.Drawing.SolidBrush $Color
		try {
			$g.FillRectangle($brush, 0, 0, $Width, $Height)
		}
		finally {
			$brush.Dispose()
		}
		$bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally {
		$g.Dispose()
		$bmp.Dispose()
	}
}

function Copy-Frame {
	param(
		[string]$From,
		[string]$To
	)
	Copy-Item -Path $From -Destination $To -Force
}

function Invoke-Case {
	param(
		[string]$Name,
		[scriptblock]$Arrange,
		[hashtable]$Contract,
		[bool]$ExpectedPass
	)

	$caseDir = Join-Path $suiteOutDir $Name
	$sequenceDir = Join-Path $caseDir "sequence"
	$reportDir = Join-Path $caseDir "report"
	New-Item -ItemType Directory -Force -Path $sequenceDir | Out-Null
	New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

	& $Arrange $sequenceDir

	$contractPath = Join-Path $caseDir "pixel-contract.json"
	$Contract | ConvertTo-Json -Depth 10 | Set-Content -Path $contractPath -Encoding UTF8

	$ok = $true
	try {
		$python = Join-Path $PSScriptRoot "assert-pixel-contract.py"
		python $python --sequence-dir $sequenceDir --contract $contractPath --out-dir $reportDir | Out-Null
		if ($LASTEXITCODE -ne 0) {
			$ok = $false
		}
	}
	catch {
		$ok = $false
	}

	$status = if ($ok -eq $ExpectedPass) { "ok" } else { "unexpected" }
	return [pscustomobject]@{
		Case = $Name
		Expected = if ($ExpectedPass) { "pass" } else { "fail" }
		Actual = if ($ok) { "pass" } else { "fail" }
		Status = $status
		Dir = $caseDir
	}
}

$baseContract = @{
	version = 1
	viewport = @{
		mode = "fixed"
		x = 0
		y = 0
		w = 64
		h = 64
		logicalWidth = 64
		logicalHeight = 64
	}
	defaults = @{
		rgbTolerance = 0
		maxErrorRatio = 0.0
	}
	globalChecks = @()
	segments = @()
}

$results = @()

# Caso positivo 1: igualdad exacta en region completa.
$contractEqualPass = $baseContract.Clone()
$contractEqualPass["segments"] = @(@{
	name = "equal-pass"
	frames = @(0, -1)
	checks = @(@{
		name = "equal-full"
		type = "equal_region"
		roi = @{ x = 0; y = 0; w = 64; h = 64 }
		rgbTolerance = 0
		maxErrorRatio = 0.0
	})
})
$results += Invoke-Case -Name "positive_equal_region" -ExpectedPass $true -Contract $contractEqualPass -Arrange {
	param($sequenceDir)
	$frame0 = Join-Path $sequenceDir "frame_000.png"
	$frame1 = Join-Path $sequenceDir "frame_001.png"
	New-SolidBitmap -Width 64 -Height 64 -Color ([System.Drawing.Color]::FromArgb(32, 96, 160)) -Path $frame0
	Copy-Frame -From $frame0 -To $frame1
}

# Caso negativo 1: igualdad falla por cambio brusco.
$contractEqualFail = $baseContract.Clone()
$contractEqualFail["segments"] = @(@{
	name = "equal-fail"
	frames = @(0, -1)
	checks = @(@{
		name = "equal-full"
		type = "equal_region"
		roi = @{ x = 0; y = 0; w = 64; h = 64 }
		rgbTolerance = 0
		maxErrorRatio = 0.0
	})
})
$results += Invoke-Case -Name "negative_equal_region" -ExpectedPass $false -Contract $contractEqualFail -Arrange {
	param($sequenceDir)
	$frame0 = Join-Path $sequenceDir "frame_000.png"
	$frame1 = Join-Path $sequenceDir "frame_001.png"
	New-SolidBitmap -Width 64 -Height 64 -Color ([System.Drawing.Color]::FromArgb(0, 0, 255)) -Path $frame0
	New-SolidBitmap -Width 64 -Height 64 -Color ([System.Drawing.Color]::FromArgb(255, 0, 0)) -Path $frame1
}

# Caso positivo 2: shifted_region_match con desplazamiento controlado.
$contractShiftPass = $baseContract.Clone()
$contractShiftPass["segments"] = @(@{
	name = "shift-pass"
	frames = @(0, -1)
	checks = @(@{
		name = "shifted"
		type = "shifted_region_match"
		roi = @{ x = 8; y = 8; w = 48; h = 48 }
		dx = 2
		dy = 0
		rgbTolerance = 0
		maxErrorRatio = 0.0
	})
})
$results += Invoke-Case -Name "positive_shifted_region" -ExpectedPass $true -Contract $contractShiftPass -Arrange {
	param($sequenceDir)
	$frame0 = Join-Path $sequenceDir "frame_000.png"
	$frame1 = Join-Path $sequenceDir "frame_001.png"
	$bmp0 = New-Object System.Drawing.Bitmap 64, 64
	$bmp1 = New-Object System.Drawing.Bitmap 64, 64
	$g0 = [System.Drawing.Graphics]::FromImage($bmp0)
	$g1 = [System.Drawing.Graphics]::FromImage($bmp1)
	try {
		$bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(20, 20, 20))
		$fg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(220, 180, 40))
		try {
			$g0.FillRectangle($bg, 0, 0, 64, 64)
			$g1.FillRectangle($bg, 0, 0, 64, 64)
			$g0.FillRectangle($fg, 12, 16, 24, 20)
			$g1.FillRectangle($fg, 14, 16, 24, 20)
		}
		finally {
			$bg.Dispose()
			$fg.Dispose()
		}
		$bmp0.Save($frame0, [System.Drawing.Imaging.ImageFormat]::Png)
		$bmp1.Save($frame1, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally {
		$g0.Dispose(); $g1.Dispose(); $bmp0.Dispose(); $bmp1.Dispose()
	}
}

# Caso negativo 2: forbidden_color_ratio detecta negro prohibido.
$contractForbiddenFail = $baseContract.Clone()
$contractForbiddenFail["globalChecks"] = @(@{
	name = "forbidden-black"
	type = "forbidden_color_ratio"
	color = @(0, 0, 0)
	colorTolerance = 0
	maxRatio = 0.01
	roi = @{ x = 0; y = 0; w = 64; h = 64 }
})
$results += Invoke-Case -Name "negative_forbidden_color" -ExpectedPass $false -Contract $contractForbiddenFail -Arrange {
	param($sequenceDir)
	$frame0 = Join-Path $sequenceDir "frame_000.png"
	$frame1 = Join-Path $sequenceDir "frame_001.png"
	New-SolidBitmap -Width 64 -Height 64 -Color ([System.Drawing.Color]::FromArgb(32, 96, 160)) -Path $frame0
	$bmp = New-Object System.Drawing.Bitmap 64, 64
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	try {
		$bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(32, 96, 160))
		$blk = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(0, 0, 0))
		try {
			$g.FillRectangle($bg, 0, 0, 64, 64)
			$g.FillRectangle($blk, 8, 8, 24, 24)
		}
		finally {
			$bg.Dispose(); $blk.Dispose()
		}
		$bmp.Save($frame1, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally {
		$g.Dispose(); $bmp.Dispose()
	}
}

$summary = @()
$summary += "# Pixel Assert Selftest"
$summary += ""
$summary += "| Case | Expected | Actual | Status |"
$summary += "|---|---:|---:|---:|"
foreach ($item in $results) {
	$summary += "| $($item.Case) | $($item.Expected) | $($item.Actual) | $($item.Status) |"
}

$summaryPath = Join-Path $suiteOutDir "selftest-summary.md"
$jsonPath = Join-Path $suiteOutDir "selftest-results.json"
$summary | Set-Content -Path $summaryPath -Encoding UTF8
$results | ConvertTo-Json -Depth 5 | Set-Content -Path $jsonPath -Encoding UTF8

$unexpected = @($results | Where-Object { $_.Status -ne "ok" })

[pscustomobject]@{
	OutDir = $suiteOutDir
	Summary = $summaryPath
	Results = $jsonPath
	Cases = $results.Count
	Unexpected = $unexpected.Count
} | Format-List

if ($unexpected.Count -gt 0) {
	throw "Pixel Assert selftest failed: $($unexpected.Count) unexpected result(s)."
}
