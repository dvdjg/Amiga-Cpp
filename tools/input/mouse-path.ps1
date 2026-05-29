param(
    [string]$From = "16,16",
    [string]$To = "304,240",
    [string]$Control = "",
    [string]$Control2 = "",
    [int]$Steps = 48,
    [int]$DurationMs = 800,
    [int]$Button = 0,
    [string]$Screenshot = "",
    [switch]$Click,
    [switch]$Drag,
    [int]$Port = 2345
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Resolve-Path (Join-Path $ScriptDir "..\..")

# This wrapper keeps the usual PowerShell workflow for the project while the
# real implementation lives in Node, next to the existing WinUAE runner.  Tests
# can call this script directly, and CI/regression scripts can still import the
# JavaScript module shape if they need finer control later.
$argsList = @(
    (Join-Path $Root "tools\input\mouse-path.mjs"),
    "--from", $From,
    "--to", $To,
    "--steps", $Steps,
    "--duration-ms", $DurationMs,
    "--button", $Button,
    "--port", $Port
)

if ($Control -ne "") {
    $argsList += @("--control", $Control)
}

if ($Control2 -ne "") {
    $argsList += @("--control2", $Control2)
}

if ($Screenshot -ne "") {
    $argsList += @("--screenshot", $Screenshot)
}

if ($Click) {
    $argsList += "--click"
}

if ($Drag) {
    $argsList += "--drag"
}

node @argsList
if ($LASTEXITCODE -ne 0) {
    throw "La automatizacion del raton de WinUAE fallo."
}
