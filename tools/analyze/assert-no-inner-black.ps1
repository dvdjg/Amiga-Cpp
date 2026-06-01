param(
	[Parameter(Mandatory = $true)]
	[string]$Source,

	[double]$MaxBlackRatio = 0.001
)

$ErrorActionPreference = "Stop"

# Detector ligero para demos con tiles simbolicos de alto contraste.
#
# La captura de WinUAE incluye un borde negro alrededor de la pantalla Amiga. Este
# script localiza primero el rectangulo no negro y despues comprueba que dentro de
# esa ventana visible no aparezcan manchas negras inesperadas. La demo 101 no usa
# negro en sus tiles; si `COPJMP1` se dispara a media pantalla o un puntero de
# bitplane se corrompe, suelen aparecer bloques negros dentro del playfield. Es una
# comprobacion barata que complementa FrameScope y evita depender solo de inspeccion
# visual humana o de un modelo de vision.
$resolved = Resolve-Path $Source
$python = @'
import sys
from pathlib import Path
from PIL import Image

source = Path(sys.argv[1])
max_black_ratio = float(sys.argv[2])
frames = sorted(source.glob("frame_*.png")) if source.is_dir() else [source]
if not frames:
    raise SystemExit(f"No frame_*.png files found in {source}")

failed = []
for frame in frames:
    image = Image.open(frame).convert("RGB")
    width, height = image.size
    pixels = image.load()
    xs = []
    ys = []
    for y in range(height):
        for x in range(width):
            if pixels[x, y] != (0, 0, 0):
                xs.append(x)
                ys.append(y)
    if not xs:
        failed.append((frame.name, "empty", 1.0, 0, 0))
        continue

    left, top, right, bottom = min(xs), min(ys), max(xs), max(ys)
    black = 0
    total = 0
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            total += 1
            if pixels[x, y] == (0, 0, 0):
                black += 1
    ratio = black / total if total else 1.0
    if ratio > max_black_ratio:
        failed.append((frame.name, "inner-black", ratio, black, total))

if failed:
    for name, kind, ratio, black, total in failed:
        print(f"{name}: {kind} ratio={ratio:.6f} black={black} total={total}", file=sys.stderr)
    raise SystemExit(1)

print(f"OK {len(frames)} frame(s), max inner black ratio <= {max_black_ratio}")
'@

$temp = New-TemporaryFile
try {
	Set-Content -LiteralPath $temp -Value $python -Encoding UTF8
	python $temp $resolved $MaxBlackRatio
	if ($LASTEXITCODE -ne 0) {
		throw "La secuencia contiene negro interno inesperado."
	}
} finally {
	Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
