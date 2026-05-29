param(
	[Parameter(Mandatory = $true)]
	[string]$Demo,

	[int]$WaitMs = 18000,

	[string]$Screenshot = "",

	[string]$MouseFrom = "",

	[string]$MouseTo = "",

	[string]$MouseControl = "",

	[int]$MouseSteps = 48,

	[int]$MouseDurationMs = 800,

	[int]$MouseButton = 0,

	[switch]$MouseClick,

	[switch]$MouseDrag,

	[switch]$KeepRunning
)

$ErrorActionPreference = "Stop"

$runner = Join-Path $PSScriptRoot "run-demo.mjs"
$argsList = @($runner, $Demo, "--wait-ms", $WaitMs)

if ($Screenshot -ne "") {
	$argsList += @("--screenshot", $Screenshot)
}

if ($MouseFrom -ne "" -or $MouseTo -ne "") {
	$argsList += @("--mouse-from", $MouseFrom, "--mouse-to", $MouseTo, "--mouse-steps", $MouseSteps, "--mouse-duration-ms", $MouseDurationMs, "--mouse-button", $MouseButton)
}

if ($MouseControl -ne "") {
	$argsList += @("--mouse-control", $MouseControl)
}

if ($MouseClick) {
	$argsList += "--mouse-click"
}

if ($MouseDrag) {
	$argsList += "--mouse-drag"
}

if ($KeepRunning) {
	$argsList += "--keep-running"
}

node @argsList
if ($LASTEXITCODE -ne 0) {
	throw "La ejecucion automatizada de la demo fallo."
}
