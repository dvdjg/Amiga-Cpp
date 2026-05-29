param(
	[string]$Demo = "",
	[switch]$ReleaseBuild,
	[switch]$SkipRun,
	[switch]$KeepGoing
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildScript = Join-Path $root "tools\build\build-demo.ps1"
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$analyzeScript = Join-Path $root "tools\analyze\analyze-demo.ps1"

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportDir = Join-Path $root "out\regression\$timestamp"
New-Item -ItemType Directory -Force $reportDir | Out-Null

if ($Demo) {
	$demoDirs = @(Resolve-Path (Join-Path $root $Demo))
} else {
	$demoDirs = @(Get-ChildItem (Join-Path $root "demos") -Directory | Sort-Object Name | Select-Object -ExpandProperty FullName)
}

if ($demoDirs.Count -eq 0) {
	throw "No se encontraron demos para ejecutar."
}

$results = @()
$markdown = @()
$markdown += "# Regression $timestamp"
$markdown += ""
$markdown += "| Demo | Build | Run | Analyze | Notes |"
$markdown += "|---|---:|---:|---:|---|"

foreach ($demoPath in $demoDirs) {
	$demoName = Split-Path $demoPath -Leaf
	$relativeDemo = "demos\$demoName"
	$result = [ordered]@{
		demo = $demoName
		build = "pending"
		run = if ($SkipRun) { "skipped" } else { "pending" }
		analyze = "pending"
		notes = ""
	}

	try {
		Write-Host "== ${demoName}: build =="
		$buildArgs = @("-ExecutionPolicy", "Bypass", "-File", $buildScript, $relativeDemo)
		if (-not $ReleaseBuild) {
			$buildArgs += "-DebugBuild"
		}
		& powershell @buildArgs
		if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
		$result.build = "ok"

		if (-not $SkipRun) {
			Write-Host "== ${demoName}: run =="
			& powershell -ExecutionPolicy Bypass -File $runScript $relativeDemo
			if ($LASTEXITCODE -ne 0) { throw "Run failed with exit code $LASTEXITCODE" }
			$result.run = "ok"
		}

		Write-Host "== ${demoName}: analyze =="
		& powershell -ExecutionPolicy Bypass -File $analyzeScript $relativeDemo
		if ($LASTEXITCODE -ne 0) { throw "Analyze failed with exit code $LASTEXITCODE" }
		$result.analyze = "ok"
	}
	catch {
		$result.notes = $_.Exception.Message
		if ($result.build -eq "pending") { $result.build = "fail" }
		elseif ($result.run -eq "pending") { $result.run = "fail" }
		elseif ($result.analyze -eq "pending") { $result.analyze = "fail" }

		if (-not $KeepGoing) {
			$results += [pscustomobject]$result
			$markdown += "| $($result.demo) | $($result.build) | $($result.run) | $($result.analyze) | $($result.notes) |"
			break
		}
	}

	$results += [pscustomobject]$result
	$markdown += "| $($result.demo) | $($result.build) | $($result.run) | $($result.analyze) | $($result.notes) |"
}

$jsonPath = Join-Path $reportDir "regression-report.json"
$mdPath = Join-Path $reportDir "regression-report.md"
$results | ConvertTo-Json -Depth 5 | Out-File -Encoding ascii $jsonPath
$markdown | Out-File -Encoding ascii $mdPath

$failed = @($results | Where-Object { $_.build -ne "ok" -or ($_.run -ne "ok" -and $_.run -ne "skipped") -or $_.analyze -ne "ok" })

Write-Host ""
Write-Host "Regression report:"
Write-Host $mdPath

if ($failed.Count -gt 0) {
	throw "Regression failed: $($failed.Count) demo(s)."
}

Write-Host "Regression OK: $($results.Count) demo(s)."
