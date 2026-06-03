param(
	[Parameter(Mandatory = $true)]
	[string]$SequenceDir,

	[Parameter(Mandatory = $true)]
	[string]$Contract,

	[string]$RunReport = "",

	[string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$resolvedSequenceDir = (Resolve-Path $SequenceDir).Path
$resolvedContract = (Resolve-Path $Contract).Path

$pythonScript = Join-Path $PSScriptRoot "assert-pixel-contract.py"
if (-not (Test-Path $pythonScript)) {
	throw "No existe el script Python de aserciones: $pythonScript"
}

$args = @(
	$pythonScript,
	"--sequence-dir", $resolvedSequenceDir,
	"--contract", $resolvedContract
)

if ($RunReport -ne "") {
	$args += @("--run-report", (Resolve-Path $RunReport).Path)
}

if ($OutDir -ne "") {
	New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
	$args += @("--out-dir", (Resolve-Path $OutDir).Path)
}

python @args
if ($LASTEXITCODE -ne 0) {
	throw "Pixel assertions failed."
}
