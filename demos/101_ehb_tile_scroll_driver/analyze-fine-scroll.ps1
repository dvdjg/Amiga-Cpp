param(
	[switch]$Warp
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$runScript = Join-Path $root "tools\run\run-demo.ps1"
$sequenceDir = Join-Path $root "out\run\101_ehb_tile_scroll_driver\sequence"
$runReport = Join-Path $root "out\run\101_ehb_tile_scroll_driver\run-report.json"

# Captura el cruce mas delicado del scroll hardware: fine X 14 -> 15 -> 0 -> 1.
# La camara de la demo mantiene cada pixel varios VBlanks para que el runner pueda
# tomar capturas sin depender de timeouts largos ni de suerte. Este test no intenta
# juzgar si la escena "parece bonita"; mide dos invariantes concretos:
#
# - el borde izquierdo del playfield no debe saltar al cruzar de fine 15 a fine 0;
# - el contenido debe desplazarse un pixel lowres por paso, que en la captura PNG
#   de WinUAE equivale a dos pixels horizontales.
$python = @'
import json
import sys
from pathlib import Path
from PIL import Image
import numpy as np

sequence_dir = Path(sys.argv[1])
run_report = Path(sys.argv[2])
expected_camera = [int(value) for value in sys.argv[3].split(",")]
expected_shifts = [int(value) for value in sys.argv[4].split(",")]
frames = sorted(sequence_dir.glob("frame_*.png"))
if len(frames) != len(expected_camera):
    raise SystemExit(f"Expected {len(expected_camera)} fine-scroll frames, got {len(frames)}")

report = json.loads(run_report.read_text(encoding="utf-8"))
telemetry = []
for frame in report["sequence"]["frames"]:
    detail = int(frame["runStatus"]["detail"])
    camera_x = (detail >> 16) & 0xff
    fine_x = camera_x & 15
    telemetry.append((frame["targetCameraX"], frame["runStatus"]["frame"], camera_x, fine_x))

if [item[2] for item in telemetry] != expected_camera:
    raise SystemExit(f"Fine-scroll capture does not match expected cameraX: {telemetry}")

images = [Image.open(path).convert("RGB") for path in frames]
bounds = []
for path, image in zip(frames, images):
    pixels = image.load()
    xs = []
    ys = []
    for y in range(image.height):
        for x in range(image.width):
            if pixels[x, y] != (0, 0, 0):
                xs.append(x)
                ys.append(y)
    if not xs:
        raise SystemExit(f"{path.name}: empty frame")
    bounds.append((min(xs), min(ys), max(xs), max(ys)))

lefts = [box[0] for box in bounds]
if max(lefts) - min(lefts) > 2:
    raise SystemExit(f"Left edge pops during fine scroll: {list(zip([p.name for p in frames], lefts))}")

left = min(box[0] for box in bounds)
top = min(box[1] for box in bounds)
right = max(box[2] for box in bounds) + 1
bottom = max(box[3] for box in bounds) + 1
arrays = [np.asarray(image.crop((left, top, right, bottom)), dtype=np.int16) for image in images]

def best_dx(a, b):
    best = None
    for dx in range(-12, 13):
        if dx < 0:
            aa = a[:, :dx, :]
            bb = b[:, -dx:, :]
        elif dx > 0:
            aa = a[:, dx:, :]
            bb = b[:, :-dx, :]
        else:
            aa = a
            bb = b
        err = float(np.mean(np.abs(aa - bb)))
        if best is None or err < best[1]:
            best = (dx, err)
    return best

shifts = [best_dx(arrays[i], arrays[i + 1]) for i in range(len(arrays) - 1)]
if [shift[0] for shift in shifts] != expected_shifts:
    raise SystemExit(f"Unexpected fine-scroll shifts: {shifts}")

print(f"OK fine scroll transition: cameraX={expected_camera}, shifts={expected_shifts}")
'@

function Invoke-FineScrollCase {
	param(
		[string]$CameraXTargets,
		[string]$ExpectedShifts
	)

	$runArgs = @(
		"-ExecutionPolicy", "Bypass",
		"-File", $runScript,
		"demos\101_ehb_tile_scroll_driver",
		"-SettleMs", "0",
		"-SequenceCameraX", $CameraXTargets
	)
	if ($Warp) {
		$runArgs += "-Warp"
	}
	& powershell @runArgs
	if ($LASTEXITCODE -ne 0) {
		throw "No se pudo capturar la transicion fine scroll cameraX=$CameraXTargets."
	}

	$temp = New-TemporaryFile
	try {
		Set-Content -LiteralPath $temp -Value $python -Encoding UTF8
		python $temp $sequenceDir $runReport $CameraXTargets $ExpectedShifts
		if ($LASTEXITCODE -ne 0) {
			throw "La transicion fine scroll no es continua."
		}
	} finally {
		Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
	}
}

Invoke-FineScrollCase -CameraXTargets "94,95,96,97" -ExpectedShifts "-2,-2,-2"
Invoke-FineScrollCase -CameraXTargets "112,111,110,109" -ExpectedShifts "2,2,2"
