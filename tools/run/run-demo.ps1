param(
	[Parameter(Mandatory = $true)]
	[string]$Demo,

	[int]$WaitMs = 18000,

	[int]$ReadyTimeoutMs = 4000,

	[int]$SideChannelTimeoutMs = 6000,

	[int]$SideChannelPort = 2346,

	[int]$LoadTimeoutMs = 20000,

	[int]$SettleMs = 500,

	[string]$Screenshot = "",

	[string]$MouseFrom = "",

	[string]$MouseTo = "",

	[string]$MouseControl = "",

	[int]$MouseSteps = 48,

	[int]$MouseDurationMs = 800,

	[int]$MouseButton = 0,

	[switch]$MouseClick,

	[switch]$MouseDrag,

	[switch]$KeepRunning,

	[int]$SequenceFrames = 0,

	[int]$SequenceIntervalMs = 100,

	[switch]$AllowTimeoutFallback
)

$ErrorActionPreference = "Stop"

$runner = Join-Path $PSScriptRoot "run-demo.mjs"
$argsList = @(
	$runner,
	$Demo,
	"--wait-ms", $WaitMs,
	"--ready-timeout-ms", $ReadyTimeoutMs,
	"--side-channel-timeout-ms", $SideChannelTimeoutMs,
	"--side-channel-port", $SideChannelPort,
	"--load-timeout-ms", $LoadTimeoutMs,
	"--settle-ms", $SettleMs
)

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

if ($SequenceFrames -gt 0) {
	$argsList += @("--sequence-frames", $SequenceFrames, "--sequence-interval-ms", $SequenceIntervalMs)
}

if ($AllowTimeoutFallback) {
	$argsList += "--allow-timeout-fallback"
}

node @argsList
if ($LASTEXITCODE -ne 0) {
	throw "La ejecucion automatizada de la demo fallo."
}
