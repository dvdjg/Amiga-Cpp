param(
	[Parameter(Mandatory = $true)]
	[string]$Demo,

	[switch]$DebugBuild,
	[switch]$Clean
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$demoPath = Resolve-Path (Join-Path $root $Demo)
$demoName = Split-Path $demoPath -Leaf

function Find-AmigaBinPath {
	if ($env:AMIGA_BIN_PATH -and (Test-Path $env:AMIGA_BIN_PATH)) {
		return (Resolve-Path $env:AMIGA_BIN_PATH).Path
	}

	$candidates = @(
		"$env:USERPROFILE\.cursor\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32",
		"$env:USERPROFILE\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32"
	)

	foreach ($candidate in $candidates) {
		if (Test-Path (Join-Path $candidate "opt\bin\m68k-amiga-elf-g++.exe")) {
			return $candidate
		}
	}

	throw "No se encontro AMIGA_BIN_PATH ni una instalacion conocida del plugin amiga-debug."
}

$amigaBin = Find-AmigaBinPath
$gcc = Join-Path $amigaBin "opt\bin\m68k-amiga-elf-gcc.exe"
$gxx = Join-Path $amigaBin "opt\bin\m68k-amiga-elf-g++.exe"
$as = Join-Path $amigaBin "opt\bin\m68k-amiga-elf-as.exe"
$elf2hunk = Join-Path $amigaBin "elf2hunk.exe"
$objdump = Join-Path $amigaBin "opt\bin\m68k-amiga-elf-objdump.exe"
$sdkDir = Join-Path $amigaBin "opt\m68k-amiga-elf\sys-include"

$objDir = Join-Path $root "obj\demos\$demoName"
$outDir = Join-Path $root "out\demos\$demoName"

if ($Clean) {
	if (Test-Path $objDir) { Remove-Item -Recurse -Force $objDir }
	if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
}

New-Item -ItemType Directory -Force $objDir | Out-Null
New-Item -ItemType Directory -Force $outDir | Out-Null

$optimization = if ($DebugBuild) { "-O1" } else { "-Ofast" }
$commonFlags = @(
	"-g",
	"-MP",
	"-MMD",
	"-m68000",
	$optimization,
	"-nostdlib",
	"-Wextra",
	"-Wno-unused-function",
	"-Wno-volatile-register-var",
	"-fomit-frame-pointer",
	"-fno-exceptions",
	"-ffunction-sections",
	"-fdata-sections",
	"-I$root",
	"-I$(Join-Path $root 'engine\include')",
	"-I$sdkDir"
)

$cppFlags = $commonFlags + @(
	"-std=gnu++23",
	"-fno-rtti",
	"-fno-threadsafe-statics",
	"-fno-use-cxa-atexit"
)

$cFlags = $commonFlags + @(
	"-std=gnu11",
	"-fno-tree-loop-distribution"
)

$sources = @()
$sources += Get-ChildItem (Join-Path $root "engine\src") -Recurse -Filter *.cpp
$sources += Get-ChildItem (Join-Path $demoPath "src") -Recurse -Filter *.cpp

$objects = @()

function Object-Path-For([string]$sourcePath) {
	$rootText = $root.Path.TrimEnd('\')
	$relative = $sourcePath
	if ($sourcePath.StartsWith($rootText)) {
		$relative = $sourcePath.Substring($rootText.Length).TrimStart('\')
	}
	$safe = $relative -replace "[:\\/ ]", "_"
	return Join-Path $objDir ($safe + ".o")
}

foreach ($source in $sources) {
	$obj = Object-Path-For $source.FullName
	$objects += $obj
	Write-Host "C++ $($source.FullName)"
	& $gxx @cppFlags -c -o $obj $source.FullName
	if ($LASTEXITCODE -ne 0) { throw "Fallo compilando $($source.FullName)" }
}

$supportC = Join-Path $root "support\gcc8_c_support.c"
$supportCObj = Join-Path $objDir "support_gcc8_c_support.o"
$objects += $supportCObj
Write-Host "C   $supportC"
& $gcc @cFlags -c -o $supportCObj $supportC
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando gcc8_c_support.c" }

$supportAsm = Join-Path $root "support\gcc8_a_support.s"
$supportAsmObj = Join-Path $objDir "support_gcc8_a_support.o"
$objects += $supportAsmObj
Write-Host "ASM $supportAsm"
& $as -mcpu=68000 -g --register-prefix-optional "-I$sdkDir" -o $supportAsmObj $supportAsm
if ($LASTEXITCODE -ne 0) { throw "Fallo ensamblando gcc8_a_support.s" }

$elf = Join-Path $outDir "$demoName.elf"
$exe = Join-Path $outDir "$demoName.exe"
$map = Join-Path $outDir "$demoName.map"
$listing = Join-Path $outDir "$demoName.s"

$ldFlags = @(
	"-Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$map"
)

Write-Host "LINK $elf"
& $gxx @commonFlags @ldFlags @objects -o $elf
if ($LASTEXITCODE -ne 0) { throw "Fallo enlazando $elf" }

Write-Host "HUNK $exe"
& $elf2hunk $elf $exe
if ($LASTEXITCODE -ne 0) { throw "Fallo generando hunk $exe" }

& $objdump --disassemble --no-show-raw-ins --visualize-jumps -S $elf | Out-File -Encoding ascii $listing

Write-Host "OK $exe"
