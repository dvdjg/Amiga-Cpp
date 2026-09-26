# Plan de organización de las demos

Plan vigente para ordenar `demos/`: separar **técnicas de hardware** de **features portables**, con
un eje de **plataforma** transversal y **paridad de comportamiento** entre plataformas. Decisiones
adoptadas (ver §6). La estructura canónica de directorios sigue en [`../../STRUCTURE.md`](../../STRUCTURE.md)
y la numeración en [`../../ai-dev-environment/NUMBERING.md`](../../ai-dev-environment/NUMBERING.md).

## 1. Las dos clases de demo

- **Técnica (hardware)**: intrínsecamente de una plataforma. Enseñan Copper/Blitter/Paula (Amiga),
  Shifter/YM/MFP (Atari ST/STE), VDP/Z80 (Megadrive). No tienen equivalente portable.
- **Feature (portable)**: lógica de juego/motor **compartida**, con una **variante por plataforma**
  que solo cambia backend + assets. Objetivo: **paridad de comportamiento** entre plataformas (un
  mus, un UI o una simulación deben jugarse igual en Amiga, Atari ST, Megadrive y PC).

## 2. Plataformas y variantes de build

- **Plataforma (familia)**: `amiga` · `atarist` · `megadrive` · `pc`.
- **Variante de build**: `A500`/`A1200` (OCS/ECS/AGA), `ST`/`STE`, etc. Es un **eje de build**
  (`TARGET_MACHINE`), no un directorio: el proyecto genera los binarios de una u otra variante y
  quedan **distinguidos por nombre** (el `CONFIG_ID` ya incluye el perfil, p. ej. `A500_debug` vs
  `A1200_debug`), de modo que no se machacan. Los **assets** específicos de variante también llevan
  el perfil en el nombre.
- **`host`** no es plataforma de demo: es para pruebas técnicas/algorítmicas (`tests/host/`,
  `playground/`). El **backend de PC/SDL** (plataforma `pc`) no es de primera clase todavía: se abre
  cuando Amiga esté al 100 %, igual que Atari ST/Megadrive.

## 3. Árbol

```
demos/
├── techniques/                      # solo hardware, por familia
│   ├── amiga/
│   │   ├── copper/   blitter/   sprites/   playfield/   c2p/   input/   audio/   io/   debug/
│   │   └── aga/      (técnicas AGA: FMODE, HAM8, chunky…)
│   ├── atarist/      st/   ste/
│   └── megadrive/    vdp/   z80/
│
├── features/                        # portables, con paridad por plataforma
│   ├── ui/         amiga/  atarist/  megadrive/  pot/   NNN_<nombre>/
│   ├── cards/      mus/    amiga/  atarist/  megadrive/  NNN_<nombre>/
│   ├── board/      chess/  go/     amiga/  …
│   ├── sim/        amiga/  atarist/  megadrive/  NNN_<nombre>/
│   ├── emulation/  amiga/  …       NNN_<nombre>/    # retroemulación
│   ├── audio/      amiga/  …
│   └── scene/  graphics/  …
│
└── README.md
```

Regla `techniques` vs `features`:

- `techniques/<familia>/<categoria>/**`: usa vocabulario de chipset (`eng/platform/<familia>`,
  registros `$dff`, VDP, MFP…). Es de hardware.
- `features/<feature>/<plataforma>/**`: la lógica vive en el **engine** (anillo 0) y/o en el
  `common/` de la feature; el directorio de plataforma es un **adaptador fino** (`main()`, backend,
  assets, mapping de input). No debe usar hardware directo.

## 4. Paridad entre plataformas

- La **lógica portable** de cada feature vive en el **engine** (anillo 0), no en la demo. La demo
  de cada plataforma es un adaptador.
- **Matriz de paridad** (`feature × plataforma`): doc con ✅ existe / — pendiente, y un **contrato de
  equivalencia** (misma secuencia de decisiones → mismo resultado lógico) que se prueba en host con
  el backend de pruebas. Es lo que hace honesto el «equivalente entre plataformas».

## 5. Numeración (anti-colisión)

- **Ámbito del número = (clase, feature/categoría, plataforma)**: p. ej.
  `features/ui/amiga/007_foo`, `features/ui/megadrive/007_foo` y
  `techniques/amiga/copper/020_basic` **no colisionan** (el path difiere).
- **Id de build/out**: `features/<…>/NNN_<tema>` usa la **ruta** relativa a `demos/features/`
  (p. ej. `features/ui/amiga/007_menu` → `ui_amiga_007_menu`), de modo que la **misma demo en
  varias plataformas** no se machaca; `techniques/…` y los tests usan el **leaf** (nombres únicos
  por construcción). Los **perfiles** (`A500`/`A1200`/`ST`/`STE`) van en el `CONFIG_ID` del binario
  (p. ej. `A500_debug` vs `A1200_debug`).
- **Assets por variante**: si una demo genera assets dependientes del perfil, van a
  `out/assets/<pipeline>/<MACHINE_ID>/…` (o con el perfil en el nombre). El `prebuild.sh` recibe
  `MACHINE_ID`/`TARGET_MACHINE` en el entorno (lo exporta `build-demo.sh`).
- `tools/check/demo-numbering.mjs` valida **por ámbito** (sin duplicados dentro de cada ámbito y sin
  leafs repetidos); `tools/check/next-number.mjs <ámbito>` da el siguiente libre de un ámbito.
  `NUMBERING.md` reserva **sub-bloques por ámbito** solo cuando dos ramas tocan el mismo.

## 6. Decisiones adoptadas

1. **Dos raíces hermanas**: `demos/techniques/` y `demos/features/`.
2. **A1200/STE como variante de build** (`TARGET_MACHINE`), no como directorio; binarios y assets
   distinguidos por nombre (perfil en `CONFIG_ID`/nombre de asset).
3. **Lógica portable de cada feature en el engine** (la demo es adaptador por plataforma).
4. **`pc` (SDL) no es de primera clase** hasta que Amiga esté al 100 %; igual que Atari ST/Megadrive.
   Aun así, **el core no hace nada Amiga-only** (lo vigila `platform-boundaries.mjs`).
5. Categorías nuevas: **`sim`** (simulación) y **`emulation`** (retroemulación), como features.
6. **Las demos son tutoriales** (regla §1.12 de `AGENTS.md`): cada demo enseña el camino de la fachada
   con comentarios de intención, sin bajo nivel gratuito (registros, punteros, `BlitJob`, backend) y
   sin narrar cronología. La plantilla de estilo es `demos/techniques/amiga/playfield/126_fast_bobs`;
   el `README.md` presenta efecto + técnica (ficha en `docs/reference/`) + contrato ilustrado.

## 7. Migración (por lotes)

1. Fijar el esquema en `STRUCTURE.md` + `NUMBERING.md`; adaptar `build-demo.sh` (id por ruta),
   `demo-numbering.mjs` y `next-number.mjs`.
2. Mover `techniques/<familia>/<categoria>/` (mapeo casi 1:1 de las demos actuales) y reapuntar
   referencias.
3. Extraer `features/` (ui, cards, board, sim, emulation, audio) con la lógica en el engine.
4. Añadir `demo-platform-boundaries.mjs` (features sin hardware directo) y un `README.md` por ámbito.
5. Verificar cada lote con `tools/run-host-tests.sh` + builds de las demos movidas.
