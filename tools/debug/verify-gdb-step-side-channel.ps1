param(
	[string]$Demo = "demos\030_ehb_palette_zones",
	[int]$Steps = 3,
	[int]$SideChannelPort = 2346,
	[switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "verify-gdb-step-side-channel.mjs"
$argsList = @(
	$script,
	"--demo", $Demo,
	"--steps", $Steps,
	"--side-channel-port", $SideChannelPort
)

if ($SkipBuild) {
	$argsList += "--skip-build"
}

node @argsList
if ($LASTEXITCODE -ne 0) {
	throw "La prueba GDB step + canal lateral fallo."
}
