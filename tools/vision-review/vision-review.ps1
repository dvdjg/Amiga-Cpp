param(
	[Parameter(Mandatory = $true)]
	[string]$Source,

	[string]$Frames = "",

	[string]$Profile = "generic-frame-diff",

	[string]$RunReport = "",

	[string]$FrameScopeReport = "",

	[string]$Provider = "",

	[ValidateSet("", "multi-image", "contact-sheet")]
	[string]$SendMode = "",

	[string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function Resolve-RepoRoot {
	return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Add-OptionalArg {
	param(
		[System.Collections.Generic.List[string]]$ArgList,
		[string]$Name,
		[string]$Value
	)

	if ($Value -ne "") {
		$ArgList.Add($Name)
		$ArgList.Add($Value)
	}
}

function New-VisionContactSheet {
	param(
		[string]$RequestJson,
		[string]$OutputPath
	)

	$request = Get-Content $RequestJson -Raw | ConvertFrom-Json
	$frames = @($request.frames)
	if ($frames.Count -eq 0) {
		throw "Vision Review no tiene frames para montar una hoja de contacto."
	}

	$thumbW = 220
	$thumbH = 174
	$labelH = 38
	$columns = [Math]::Min(4, [Math]::Max(1, $frames.Count))
	$rows = [Math]::Ceiling($frames.Count / $columns)
	$sheet = New-Object System.Drawing.Bitmap ($thumbW * $columns), (($thumbH + $labelH) * $rows)
	$graphics = [System.Drawing.Graphics]::FromImage($sheet)
	try {
		$graphics.Clear([System.Drawing.Color]::FromArgb(18, 18, 18))
		$font = New-Object System.Drawing.Font "Consolas", 8
		$brush = [System.Drawing.Brushes]::White
		$muted = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(190, 190, 190))
		for ($i = 0; $i -lt $frames.Count; ++$i) {
			$frame = $frames[$i]
			$image = [System.Drawing.Bitmap]::FromFile($frame.path)
			try {
				$col = $i % $columns
				$row = [Math]::Floor($i / $columns)
				$x = $col * $thumbW
				$y = $row * ($thumbH + $labelH)
				$graphics.DrawImage($image, $x, $y + $labelH, $thumbW, $thumbH)
				$label = "pkg $($frame.packageIndex) src $($frame.sourceIndex)"
				$graphics.DrawString($label, $font, $brush, $x + 4, $y + 4)
				if ($null -ne $frame.telemetry) {
					$detail = "cam $($frame.telemetry.cameraX),$($frame.telemetry.cameraY) f=$($frame.telemetry.frame)"
					$graphics.DrawString($detail, $font, $muted, $x + 4, $y + 20)
				}
			}
			finally {
				$image.Dispose()
			}
		}
		$sheet.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally {
		$graphics.Dispose()
		$sheet.Dispose()
	}
}

$root = Resolve-RepoRoot
$script = Join-Path $PSScriptRoot "vision-review.mjs"
$node = Get-Command node -ErrorAction SilentlyContinue
if ($null -eq $node) {
	throw "Vision Review necesita node en PATH para ejecutar $script."
}

$argsList = [System.Collections.Generic.List[string]]::new()
$argsList.Add($script)
$argsList.Add("--root")
$argsList.Add($root)
$argsList.Add("--source")
$argsList.Add($Source)
$argsList.Add("--profile")
$argsList.Add($Profile)
Add-OptionalArg -ArgList $argsList -Name "--frames" -Value $Frames
Add-OptionalArg -ArgList $argsList -Name "--runReport" -Value $RunReport
Add-OptionalArg -ArgList $argsList -Name "--frameScopeReport" -Value $FrameScopeReport
Add-OptionalArg -ArgList $argsList -Name "--provider" -Value $Provider
Add-OptionalArg -ArgList $argsList -Name "--outDir" -Value $OutDir

$jsonText = & $node.Source @argsList
if ($LASTEXITCODE -ne 0) {
	throw "vision-review.mjs fallo."
}

$summary = $jsonText | ConvertFrom-Json
$contactSheet = Join-Path $summary.outDir "contact-sheet.png"
New-VisionContactSheet -RequestJson $summary.requestJson -OutputPath $contactSheet

$request = Get-Content $summary.requestJson -Raw | ConvertFrom-Json
$request | Add-Member -NotePropertyName contactSheet -NotePropertyValue $contactSheet -Force
$request | ConvertTo-Json -Depth 12 | Set-Content -Path $summary.requestJson -Encoding UTF8

$reviewReport = ""
if ($Provider -ne "") {
	$reviewArgs = [System.Collections.Generic.List[string]]::new()
	$reviewArgs.Add($script)
	$reviewArgs.Add("--reviewRequest")
	$reviewArgs.Add($summary.requestJson)
	$reviewArgs.Add("--provider")
	$reviewArgs.Add($Provider)
	Add-OptionalArg -ArgList $reviewArgs -Name "--sendMode" -Value $SendMode
	$reviewText = & $node.Source @reviewArgs
	if ($LASTEXITCODE -ne 0) {
		throw "Vision Review proveedor fallo."
	}
	$reviewSummary = $reviewText | ConvertFrom-Json
	$reviewReport = $reviewSummary.report
}

[pscustomobject]@{
	Status = "ok"
	Profile = $Profile
	SelectionReason = $summary.selectionReason
	SelectedFrames = ($summary.selectedFrames -join ",")
	OutDir = $summary.outDir
	Request = $summary.requestJson
	ContactSheet = $contactSheet
	ReviewReport = $reviewReport
} | Format-List
