# Frontend SCUMM V5 (`extract.mjs`)

Extrae juegos **SCUMM V5** (PC floppy/CD con `GF_USE_KEY`: MI1/MI2, Indy4, etc.) a **recursos por sala** y a un **SIM v1** (manifiesto, ver [`../../sim/SCHEMA.md`](../../sim/SCHEMA.md)); los bloques de guion se descompilan con `descumm` a texto legible. Es el primer paso de I0 (ver [`ROADMAP_ADVENTURE_IMPORT.md`](../../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md)).

**Estado verificado** (MI2 *Ultimate Talkie Edition*, datos PC en `../SCUMM/Games/PC/`): 127 salas en el índice (110 con datos), **1006 bloques de guion descompilados al 100 %** (`descumm -5`), 163 costumes, 229 sonidos, 9 charsets y 1200 objetos globales; SIM validado y golden test en verde (`node --test tools/adventure/sim/sim.test.mjs`).

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

# Volcar todos los bloques de una sala (RMHD/BOXD/RMIM/OBIM/OBCD/SCRP/COST/...) + room_N.json
node tools/adventure/frontends/scumm_v5/extract.mjs dump-room "<juego>/monkey2.000" --room 4

# Volcar y descomprimir (descumm) los guiones de una sala o del juego entero
node tools/adventure/frontends/scumm_v5/extract.mjs scripts "<juego>/monkey2.000" [--room N] [--no-descumm] [--descumm <exe>]

# Manifiesto SIM v1 (rooms.json)
node tools/adventure/frontends/scumm_v5/extract.mjs rooms-json "<juego>/monkey2.000" [--out <file>]
```

- Salida por defecto: `out/assets/scumm-v5/<juego>/` (gitignored; regla `out/README.md`).
- `descumm` por defecto: `../SCUMM/scummvm-tools-2.9.0/descumm.exe` (compilado con `tools/adventure/build-scummvm-tools.sh`); se puede fijar con `--descumm` o `DESCUMM`.
- El extractor **detecta la clave** (`0x69` o sin cifrado) por la cabecera y falla con mensaje claro si el contenedor no es V5.

## Pendiente de este frontend

- Decodificar `RMIM` (imagen), `BOXD/BOXM` (cajas → SIM) y `COST` (frames de actor) a estructuras semánticas (I0/I2; referencia: `room.cpp`, `boxes.cpp`, `costume.cpp`).
- Frontend **V3/V4 (LFL)** para las copias Amiga de MI1/Indy3/Loom (formato `.LFL` con índice propio; referencia: `resource_v3.cpp`/`resource_v4.cpp`).
- Los scripts se vuelcan con nombre `SCRP_<sala>_<offset>.bin/.txt`; al añadir la tabla `DSCR` (sala+offset por id) se recuperará el **id de script** para nombrarlos y enlazarlos con el SIM.
