param(
	[string]$Demo = "",
	[ValidateSet("", "BuildDebug", "BuildRelease", "Run", "Debug", "Analyze", "Sequence", "SideChannel")]
	[string]$Action = "",
	[switch]$Warp
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildScript = Join-Path $root "tools\build\build-demo.ps1"
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$analyzeScript = Join-Path $root "tools\analyze\analyze-demo.ps1"
$sequenceAnalyzer = Join-Path $root "tools\analyze\analyze-frame-sequence.ps1"
$sideChannelScript = Join-Path $root "tools\debug\winuae-side-channel.ps1"

function Get-DemoList {
	Get-ChildItem (Join-Path $root "demos") -Directory |
		Sort-Object Name |
		ForEach-Object {
			$name = $_.Name
			$exe = Join-Path $root "out\demos\$name\$name.exe"
			[pscustomobject]@{
				Name = $name
				Path = $_.FullName
				Relative = "demos\$name"
				Exe = $exe
				Built = Test-Path $exe
				SequenceScript = Join-Path $_.FullName "analyze-sequence.ps1"
			}
		}
}

function Select-Demo {
	param([object[]]$Demos)

	if ($Demo -ne "") {
		$match = @($Demos | Where-Object { $_.Name -eq $Demo -or $_.Relative -eq $Demo -or $_.Path -eq $Demo })
		if ($match.Count -eq 1) {
			return $match[0]
		}
		throw "No se encontro una demo que coincida con '$Demo'."
	}

	Write-Host ""
	Write-Host "Demos disponibles"
	Write-Host "----------------"
	for ($i = 0; $i -lt $Demos.Count; ++$i) {
		$status = if ($Demos[$i].Built) { "exe" } else { "sin build" }
		Write-Host ("{0,2}. {1,-34} {2}" -f ($i + 1), $Demos[$i].Name, $status)
	}

	$choice = Read-Host "Elige demo por numero"
	$index = 0
	if (-not [int]::TryParse($choice, [ref]$index) -or $index -lt 1 -or $index -gt $Demos.Count) {
		throw "Seleccion de demo no valida."
	}
	return $Demos[$index - 1]
}

function Select-Action {
	param([object]$SelectedDemo)

	if ($Action -ne "") {
		return $Action
	}

	Write-Host ""
	Write-Host "Acciones para $($SelectedDemo.Name)"
	Write-Host "------------------------------"
	Write-Host " 1. Build debug"
	Write-Host " 2. Build release"
	Write-Host " 3. Lanzar a ritmo real y capturar"
	Write-Host " 4. Depurar: lanzar a ritmo real y dejar WinUAE abierto"
	Write-Host " 5. Analizar ultima captura"
	Write-Host " 6. Secuencia animada si la demo la soporta"
	Write-Host " 7. Consultar canal lateral de una instancia viva"
	Write-Host " 0. Salir"

	$choice = Read-Host "Elige accion"
	switch ($choice) {
		"1" { return "BuildDebug" }
		"2" { return "BuildRelease" }
		"3" { return "Run" }
		"4" { return "Debug" }
		"5" { return "Analyze" }
		"6" { return "Sequence" }
		"7" { return "SideChannel" }
		"0" { return "Exit" }
		default { throw "Accion no valida." }
	}
}

function Invoke-Checked {
	param(
		[string]$Title,
		[string[]]$Command
	)

	Write-Host ""
	Write-Host "== $Title =="
	& $Command[0] $Command[1..($Command.Count - 1)]
	if ($LASTEXITCODE -ne 0) {
		throw "$Title fallo con codigo $LASTEXITCODE."
	}
}

function Invoke-BuildDebug {
	param([object]$SelectedDemo)
	Invoke-Checked "Build debug $($SelectedDemo.Name)" @("powershell", "-ExecutionPolicy", "Bypass", "-File", $buildScript, $SelectedDemo.Relative, "-DebugBuild")
}

function Invoke-BuildRelease {
	param([object]$SelectedDemo)
	Invoke-Checked "Build release $($SelectedDemo.Name)" @("powershell", "-ExecutionPolicy", "Bypass", "-File", $buildScript, $SelectedDemo.Relative)
}

function Invoke-RunDemo {
	param(
		[object]$SelectedDemo,
		[switch]$KeepRunning
	)

	if (-not (Test-Path $SelectedDemo.Exe)) {
		Invoke-BuildDebug $SelectedDemo
	}

	$args = @("powershell", "-ExecutionPolicy", "Bypass", "-File", $runScript, $SelectedDemo.Relative)
	if ($KeepRunning) {
		$args += "-KeepRunning"
	}
	if ($Warp) {
		$args += "-Warp"
	}

	$title = if ($KeepRunning) { "Debug $($SelectedDemo.Name)" } else { "Run $($SelectedDemo.Name)" }
	Invoke-Checked $title $args

	if ($KeepRunning) {
		Write-Host ""
		Write-Host "WinUAE queda abierto a ritmo real. Comandos utiles:"
		Write-Host "  .\tools\debug\winuae-side-channel.ps1 state"
		Write-Host "  .\tools\debug\winuae-side-channel.ps1 regs"
		Write-Host "  .\tools\debug\winuae-side-channel.ps1 pause"
		Write-Host "  .\tools\debug\winuae-side-channel.ps1 resume"
	}
}

function Invoke-Analyze {
	param([object]$SelectedDemo)
	Invoke-Checked "Analyze $($SelectedDemo.Name)" @("powershell", "-ExecutionPolicy", "Bypass", "-File", $analyzeScript, $SelectedDemo.Relative)
}

function Invoke-Sequence {
	param([object]$SelectedDemo)

	if (Test-Path $SelectedDemo.SequenceScript) {
		Invoke-Checked "Sequence $($SelectedDemo.Name)" @("powershell", "-ExecutionPolicy", "Bypass", "-File", $SelectedDemo.SequenceScript)
		return
	}

	Invoke-RunDemo $SelectedDemo
	$sequenceDir = Join-Path $root "out\run\$($SelectedDemo.Name)\sequence"
	if (-not (Test-Path $sequenceDir)) {
		Write-Host "La demo no tiene analyze-sequence.ps1 ni una secuencia previa en out\run."
		return
	}
	Invoke-Checked "Analyze sequence $($SelectedDemo.Name)" @("powershell", "-ExecutionPolicy", "Bypass", "-File", $sequenceAnalyzer, $sequenceDir)
}

function Invoke-SideChannelMenu {
	if (-not (Test-Path $sideChannelScript)) {
		throw "No existe $sideChannelScript."
	}
	Write-Host ""
	Write-Host "Canal lateral"
	Write-Host "-------------"
	Write-Host " 1. state"
	Write-Host " 2. regs"
	Write-Host " 3. pause"
	Write-Host " 4. resume"
	Write-Host " 5. audit"
	$choice = Read-Host "Comando"
	$command = switch ($choice) {
		"1" { "state" }
		"2" { "regs" }
		"3" { "pause" }
		"4" { "resume" }
		"5" { "audit" }
		default { throw "Comando lateral no valido." }
	}
	Invoke-Checked "Side channel $command" @("powershell", "-ExecutionPolicy", "Bypass", "-File", $sideChannelScript, $command)
}

$demos = @(Get-DemoList)
if ($demos.Count -eq 0) {
	throw "No hay demos en $root\demos."
}

$selectedDemo = Select-Demo $demos
$selectedAction = Select-Action $selectedDemo

switch ($selectedAction) {
	"BuildDebug" { Invoke-BuildDebug $selectedDemo }
	"BuildRelease" { Invoke-BuildRelease $selectedDemo }
	"Run" { Invoke-RunDemo $selectedDemo }
	"Debug" { Invoke-RunDemo $selectedDemo -KeepRunning }
	"Analyze" { Invoke-Analyze $selectedDemo }
	"Sequence" { Invoke-Sequence $selectedDemo }
	"SideChannel" { Invoke-SideChannelMenu }
	"Exit" { return }
	default { throw "Accion no implementada: $selectedAction" }
}
