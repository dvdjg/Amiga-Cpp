param(
	[Parameter(Mandatory = $true)]
	[string]$Demo
)

$ErrorActionPreference = "Stop"

$runner = Join-Path $PSScriptRoot "run-demo.mjs"
node $runner $Demo
if ($LASTEXITCODE -ne 0) {
	throw "La ejecucion automatizada de la demo fallo."
}
