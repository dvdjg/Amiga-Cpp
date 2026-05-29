param(
	[Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
	[string[]]$Command,

	[int]$Port = 2346
)

$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "winuae-side-channel.mjs"
$argsList = @($script) + $Command + @("--port", $Port)

node @argsList
if ($LASTEXITCODE -ne 0) {
	throw "La consulta al canal lateral de WinUAE fallo."
}
