param(
	[string]$Demo = "demos\030_ehb_palette_zones",
	[int]$SettleMs = 9000,
	[int]$SideChannelPort = 2346,
	[switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "verify-side-channel-pause-resume.mjs"
$argsList = @(
	$script,
	"--demo", $Demo,
	"--settle-ms", $SettleMs,
	"--port", $SideChannelPort
)

if ($SkipBuild) {
	$argsList += "--skip-build"
}

node @argsList
if ($LASTEXITCODE -ne 0) {
	throw "La prueba pause/resume del canal lateral fallo."
}
