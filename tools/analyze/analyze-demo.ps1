param(
	[Parameter(Mandatory = $true)]
	[string]$Demo
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$demoPath = Resolve-Path (Join-Path $root $Demo)
$demoName = Split-Path $demoPath -Leaf
$outDir = Join-Path $root "out\demos\$demoName"
$exe = Join-Path $outDir "$demoName.exe"
$elf = Join-Path $outDir "$demoName.elf"
$map = Join-Path $outDir "$demoName.map"
$screenshot = Join-Path $root "out\run\$demoName\screenshot.png"
$demoSpecificAnalyzer = Join-Path $demoPath "analyze-screenshot.ps1"

if (!(Test-Path $exe)) { throw "No existe $exe. Ejecuta primero tools/build/build-demo.ps1." }
if (!(Test-Path $elf)) { throw "No existe $elf." }
if (!(Test-Path $map)) { throw "No existe $map." }

$exeInfo = Get-Item $exe
$elfInfo = Get-Item $elf
$mapText = Get-Content $map -Raw

$requiredSymbols = @("_start", "_main")
$missing = @()
foreach ($symbol in $requiredSymbols) {
	if ($mapText -notmatch [regex]::Escape($symbol)) {
		$missing += $symbol
	}
}

if ($missing.Count -gt 0) {
	throw "Faltan simbolos esperados en el mapa: $($missing -join ', ')"
}

[pscustomobject]@{
	Demo = $demoName
	Executable = $exe
	ExeBytes = $exeInfo.Length
	ElfBytes = $elfInfo.Length
	Map = $map
	Screenshot = if (Test-Path $screenshot) { $screenshot } else { "(sin captura todavia)" }
	Status = "OK"
} | Format-List

if (Test-Path $screenshot) {
	$analyzer = if (Test-Path $demoSpecificAnalyzer) { $demoSpecificAnalyzer } else { Join-Path $PSScriptRoot "analyze-screenshot.ps1" }
	& powershell -ExecutionPolicy Bypass -File $analyzer $screenshot
	if ($LASTEXITCODE -ne 0) {
		throw "La captura existe, pero no supera el analisis visual automatico."
	}
}
