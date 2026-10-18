# Frontend SCUMM V5 (`extract.mjs`)

Extrae juegos **SCUMM V5** (PC floppy/CD con `GF_USE_KEY`: MI1/MI2, Indy4, etc.) a **recursos por sala** y a un **SIM v1** (manifiesto, ver [`../../sim/SCHEMA.md`](../../sim/SCHEMA.md)); los bloques de guion se descompilan con `descumm` a texto legible. Es el primer paso de I0 (ver [`ROADMAP_ADVENTURE_IMPORT.md`](../../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md)).

**Estado verificado** (datos PC en `../SCUMM/Games/PC/`): **MI1 *Ultimate Talkie Edition*** — 86 salas con datos, **748/748 guiones** descompilados (`descumm -5`), 150 costumes, 179 sonidos, 7 charsets, 1000 objetos globales, 86 salas con walkboxes; **MI2 *Ultimate Talkie Edition*** — 110 salas, **1006/1006 guiones**, 163 costumes, 229 sonidos. SIM validado y golden test en verde (`node --test tools/adventure/sim/sim.test.mjs`).

## Formato (contrastado con el oráculo y con datos reales)

Referencia canónica: `../SCUMM/scummvm-2026.4.0/engines/scumm/resource.cpp` (líneas citadas abajo).

- **`.000` (índice)**: secuencia de bloques `TAG` (4 ASCII) + `size` (u32 BE, incluye la cabecera de 8 B) — `readIndexFile` (`resource.cpp:253`). Los juegos con `GF_USE_KEY` van **cifrados con XOR 0x69** byte a byte — `getEncByte` (`resource.cpp:200`): v≤3 → `0xFF`; v4 → solo salas 1–899; v5 → `0x69`. Bloques: `RNAM` (nombres de sala: sala + 9 B XOR `0xFF`, hasta sala 0 — `resource.cpp:436`), `MAXS`, y las tablas `DROO`/`DSCR`/`DCOS`/`DSOU`/`DCHR` con formato `count(u16 LE)` + `count` bytes de sala/disco + `count` offsets `u32 LE` (`readResTypeList`, `resource.cpp:480`), más `DOBJ` (nº de objetos globales).
- **`.00N` (datos, N≥1)**: también XOR 0x69. Contenedor `LECF` + bloque `LOFF` + bloques `LFLF` contiguos. `LOFF` = tabla `count(u8)` + `count` × (`sala u8`, `offset u32 LE`) — lo que lee `readRoomsOffsets` (`resource.cpp:156`). El offset apunta **directamente al bloque `ROOM`** (que es el primer hijo del `LFLF`).
- **Dentro del `ROOM`** (digest real, sala 4 «shore» de MI2): `RMHD` (cabecera), `CYCL` (ciclos de paleta), `TRNS` (transparencia), `EPAL` (paleta EGA 16c), `CLUT` (paleta 256c), `SCAL` (tabla de escalas), `RMIM` (imagen de sala comprimida), `BOXD`/`BOXM` (**walkboxes** y matriz), y por objeto `OBIM` (imagen) + `OBCD` (definición).
- **En el `LFLF`, fuera del `ROOM`**: los guiones de la sala (`SCRP`, `EXCD`/`ENCD` — script de entrada — y `LSCR` locales) y los recursos usados por ella (`COST` costumes, `CHAR` charsets, `SOUN` sonidos). `descumm -5` consume los bloques `SCRP/EXCD/ENCD/LSCR` tal cual (con su cabecera).
- Los `DROO` con **disco 0** son huecos sin datos (en MI2 UE, salas 111–126); las 110 salas reales viven en `monkey2.001`.

## Uso

```bash
# Tabla del índice (bloques, cuentas, nombres de sala)
node tools/adventure/frontends/scumm_v5/extract.mjs index "<juego>/monkey2.000"

# Digest de salas (bloques de cada ROOM + sección de scripts del LFLF)
node tools/adventure/frontends/scumm_v5/extract.mjs rooms "<juego>/monkey2.000" [--room N]

# Extracción COMPLETA al workspace del juego (raw/rooms + raw/scripts + raw/index/rooms.json)
node tools/adventure/frontends/scumm_v5/extract.mjs dump-game "<juego>/monkey2.000" --out out/adventure/<juego>

# Una sola sala (bloques a raw/rooms/room_N/ + blocks.json) / solo guiones
node tools/adventure/frontends/scumm_v5/extract.mjs dump-room "<juego>/monkey2.000" --room 4
node tools/adventure/frontends/scumm_v5/extract.mjs scripts "<juego>/monkey2.000" [--room N] [--no-descumm]

# Walkboxes (BOXD/BOXM) decodificadas a decoded/walkboxes/room_N.json
node tools/adventure/frontends/scumm_v5/extract.mjs decode-boxes "<juego>/monkey2.000"

# Manifiesto SIM v1 (rooms.json)
node tools/adventure/frontends/scumm_v5/extract.mjs rooms-json "<juego>/monkey2.000" [--out <file>]
```

- Salida por defecto: el **workspace del juego** `out/adventure/<juego>/` (estructura y reglas en [`../../WORKSPACE.md`](../../WORKSPACE.md); `raw/` lo escribe solo el frontend).
- `descumm` por defecto: `../SCUMM/scummvm-tools-2.9.0/descumm.exe` (compilado con `tools/adventure/build-scummvm-tools.sh`); se puede fijar con `--descumm` o `DESCUMM`.
- El extractor **detecta la clave** (`0x69` o sin cifrado) por la cabecera y falla con mensaje claro si el contenedor no es V5.

## Pendiente de este frontend

- Decodificar `RMIM` (imagen), `OBIM/OBCD` (objetos) y `COST` (frames de actor) a estructuras semánticas (I0/I2; referencia: `room.cpp`, `costume.cpp`). Las walkboxes ya se decodifican con `decode-boxes` (referencia: `boxes.cpp`).
- Frontend **V3/V4 (LFL)** para las copias Amiga de MI1/Indy3/Loom (formato `.LFL` con índice propio; referencia: `resource_v3.cpp`/`resource_v4.cpp`).
- Los scripts se vuelcan con nombre `SCRP_<sala>_<offset>.bin/.txt` dentro de `raw/scripts/room_<N>/`; al añadir la tabla `DSCR` (sala+offset por id) se recuperará el **id de script** para nombrarlos y enlazarlos con el SIM.
