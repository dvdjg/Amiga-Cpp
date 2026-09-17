# Consulta — port ASM m68k de `flatshade-convex` (demo 116)

Ayuda para depurar una **excepción / render deformado** en una rutina ASM 68000 escrita a
mano que debería ser equivalente a su versión C++ (que funciona). No hay ASM original que
copiar: el efecto original es **C**.

---

## 1. Contexto

- **Proyecto**: engine C++23 para Amiga 500, demos en `demos/amiga/`. La demo 116
  (`116_flatshade_convex`) es un port fiel del efecto demoscene
  `demoscene-repo-orig/effects/flatshade-convex/flatshade-convex.c`: un sólido convexo
  (`pilka`) girando con sombreado plano, display 256×256×4.
- **Objetivo del port ASM**: el original es **C** con "pins" de registro:
  ```c
  register char s asm("d2") = 1;
  register char flags asm("d3") = FACE(f)->flags;
  ```
  y macros (`MULVERTEX1/2`, `DRAWLINE`). El **g++ 15 de este repo ignora esos pins**
  (`register ... asm("aN")` no obliga la asignación), así que no alcanza el codegen
  apretado. La estrategia (regla §12.3 de `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` y ya usada en
  `support/fire_loop.s`) es: **conservar la versión C++ canónica y portar las rutinas
  calientes a ASM m68k escrito a mano** (`support/flatshade_asm.s`), con un flag
  `K_FLATSHADE_ASM` que elige una u otra.

---

## 2. Entorno / toolchain (por si importa)

- Compilador C++: `m68k-amiga-elf-g++ (GCC) 15.1.0` — fork amiga-gcc/bebbo (ELF,
  freestanding). Flags: `-m68000 -std=gnu++23 -O2 -fomit-frame-pointer -fno-exceptions
  -fno-rtti` (build `--debug` interno del repo).
- Ensamblador: `m68k-amiga-elf-as (GNU Binutils) 2.44.50`, flags
  `-mcpu=68000 -g --register-prefix-optional`.
- Enlace: ELF → HUNK (`elf2hunk`). Emulador: **WinUAE-DBG** (servidor GDB + canal
  lateral), 68000/OCC ciclo-exacto.
- ABI m68k de GCC: argumentos por pila, retorno en `d0`; callee preserva `d2-d7/a2-a6`.
  Las rutinas ASM reciben sus argumentos por **memoria** en un global `extern "C"`
  (`g_fs_args`), no por pila.

---

## 3. Arquitectura del port ASM

`g_fs_args` (struct global `extern "C"`, en `.bss`/`.data`):

```cpp
struct FlatShadeAsmArgs {
    void* obj;               // 0:  puntero al Object3D
    eng::u8* planes;         // 4:  base del buffer de bitplanes (solo draw_edges)
    const eng::u16* invsqrt; // 8:  kInvSqrt[512]
    eng::s16* bbox;          // 12: g_bbox[4]
};
FlatShadeAsmArgs g_fs_args {};
void fs_update_face_visibility(void);
void fs_update_edge_visibility_convex(void);
void fs_transform_vertices(void);
void fs_draw_edges(void);
```

La demo, en `init()` y en cada `update()`, llama (si `K_FLATSHADE_ASM=1`):

```cpp
prepare_fs_args(planes, m_object);   // rellena g_fs_args
fs_update_face_visibility();
fs_update_edge_visibility_convex();
fs_transform_vertices();
```

`prepare_fs_args` (inline C++):

```cpp
inline void prepare_fs_args(eng::PlaneBytes planes, obj::Object3D& object) {
    g_fs_args.obj = &object;
    g_fs_args.planes = planes.data();
    g_fs_args.invsqrt = kInvSqrt;
    g_fs_args.bbox = g_bbox;
}
```

El `Object3D` (mismo layout que el original y que `engine/include/eng/platform/amiga/object3d.hpp`):

```
objdat=0  vertexGroups=4  edgeGroups=8  faceGroups=12  objects=16
rotate=20 (Point3D)  scale=26  translate=32
objectToWorld=38 (Mat3x3, 12×s16: m00=38 m01=40 m02=42 x=44
                                m10=46 m11=48 m12=50 y=52
                                m20=54 m21=56 m22=58 z=60)
worldToObject=62   camera=86 (cx=86 cy=88 cz=90)
```

Structs del `objdat` (offsets de byte; los índices de los grupos son offsets de byte):

```
Node3D : flags(s8)=0, point={x=2,y=4,z=6}, vertex={x=8,y=10,z=12}   (14 bytes)
Edge   : flags(s8)=0, pad=1, point[2]={2,4}                          (6 bytes)
Face   : normal[3]={0,2,4}, flags(s8)=6, material(s8)=7, count(s16)=8, indices=10
FaceIndex : vertex(s16), edge(s16)
Point3D: x,y,z (s16)
Mesh3D->data = objdat ; Mesh3D->vertexGroups/edgeGroups/faceGroups = punteros a las listas
```

`kInvSqrt` es `u16 kInvSqrt[512]`. `x << 4 = fx`, `normfx(a) = a >> 12` (aritmético).
`mul16(a,b)` = `muls.w` 16×16→32; `mulu16` = `mulu.w`; `div16(a,b)` = `divs.w`
(cociente s16 en la palabra baja). Ancho/alto = 256 → `WIDTH/2 = HEIGHT/2 = 128`.

---

## 4. La rutina problemática: `fs_transform_vertices`

### 4.1. Versión C++ canónica (FUNCIONA, verify-116 PASS)

```cpp
void transform_vertices(obj::Object3D& object) {
    eng::math3d::Mat3x3& M = object.objectToWorld;
    void* objdat = object.objdat;
    eng::s16* group = object.vertexGroups;

    eng::s32 m0 = (static_cast<eng::s32>(M.x) - eng::math2d::normfx(static_cast<eng::s32>(M.m00) * M.m01)) << 8;
    eng::s32 m1 = (static_cast<eng::s32>(M.y) - eng::math2d::normfx(static_cast<eng::s32>(M.m10) * M.m11)) << 8;
    M.z = static_cast<eng::s16>(M.z - eng::math2d::normfx(static_cast<eng::s32>(M.m20) * M.m21));

    g_bbox[0] = 32767; g_bbox[1] = -32768; g_bbox[2] = 32767; g_bbox[3] = -32768;
    do {
        eng::s16 i;
        while ((i = *group++)) {
            obj::Node3D* node = obj::node3d(objdat, i);   // objdat + (i - 2)
            if (node->flags) {
                eng::s16* pt = reinterpret_cast<eng::s16*>(node);
                eng::s16* v  = reinterpret_cast<eng::s16*>(&M);
                eng::s16 x, y, z, zp;
                eng::s32 xy, xp, yp;

                *pt++ = 0;
                x = *pt++; y = *pt++; z = *pt++;
                xy = eng::math2d::mul16(x, y);

                MULVERTEX1(xp, m0);
                MULVERTEX1(yp, m1);
                MULVERTEX2(zp);

                const eng::s16 sx = static_cast<eng::s16>(eng::math2d::div16(xp, zp) + kWidth / 2);
                const eng::s16 sy = static_cast<eng::s16>(eng::math2d::div16(yp, zp) + kHeight / 2);
                *pt++ = sx; *pt++ = sy; *pt++ = zp;

                if (sx < g_bbox[0]) g_bbox[0] = sx;
                if (sx > g_bbox[1]) g_bbox[1] = sx;
                if (sy < g_bbox[2]) g_bbox[2] = sy;
                if (sy > g_bbox[3]) g_bbox[3] = sy;
            }
        }
    } while (*group);
}
```

Macros (port 1:1 del original):

```cpp
#define MULVERTEX1(D, E) { \
    eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
    eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
    eng::s32 t2 = eng::math2d::mul16(*v++, z); \
    v++; \
    D = static_cast<eng::s32>(((eng::math2d::mul16(t0, t1) + t2 - xy) >> 4) + E); \
}
#define MULVERTEX2(D) { \
    eng::s16 t0 = static_cast<eng::s16>((*v++) + y); \
    eng::s16 t1 = static_cast<eng::s16>((*v++) + x); \
    eng::s32 t2 = eng::math2d::mul16(*v++, z); \
    eng::s16 t3 = *v++; \
    D = static_cast<eng::s32>(eng::math2d::normfx(eng::math2d::mul16(t0, t1) + t2 - xy)) + t3; \
}
```

> Nota: `v` recorre la matriz. `MULVERTEX1(xp,m0)` lee `m00,m01,m02` (salta `x`);
> `MULVERTEX1(yp,m1)` lee `m10,m11,m12` (salta `y`); `MULVERTEX2(zp)` lee
> `m20,m21,m22,z`. `t0 = m + y` (¡`y` es `point.y`!) y `t1 = m + x` (`point.x`).
> `MULVERTEX2` usa `t3 = M.z` (la `z` del eje, ya modificada por el preámbulo).

### 4.2. La versión ASM escrita a mano (BUG: render deformado)

Sintaxis gas (`--register-prefix-optional`), sin `.cfi`. `a3 = obj`, `a0 = objdat`,
`a1 = vertexGroups`, `a4 = bbox`:

```asm
	.globl	fs_transform_vertices
fs_transform_vertices:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	-20(sp),sp			/* slots: zp/xp/yp/m0/m1 */
	lea	g_fs_args,a2			/* args */
	movea.l	0(a2),a3			/* Object3D* */
	movea.l	0(a3),a0			/* objdat */
	movea.l	4(a3),a1			/* vertexGroups */
	movea.l	12(a2),a4			/* bbox */
	/* m0 = (M.x - normfx(m00*m01)) << 8 */
	move.w	44(a3),d0
	ext.l	d0
	move.w	38(a3),d1
	ext.l	d1
	move.w	40(a3),d2
	ext.l	d2
	muls.w	d2,d1				/* m00*m01 */
	moveq	#12,d2
	asr.l	d2,d1				/* normfx */
	sub.l	d1,d0
	lsl.l	#8,d0
	move.l	d0,12(sp)			/* m0 */
	/* m1 = (M.y - normfx(m10*m11)) << 8 */
	move.w	52(a3),d0
	ext.l	d0
	move.w	46(a3),d1
	ext.l	d1
	move.w	48(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	lsl.l	#8,d0
	move.l	d0,16(sp)			/* m1 */
	/* M.z = M.z - normfx(m20*m21) */
	move.w	60(a3),d0
	ext.l	d0
	move.w	54(a3),d1
	ext.l	d1
	move.w	56(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	move.w	d0,60(a3)			/* M.z (s16) */
	/* bbox reset */
	move.w	#32767,0(a4)
	move.w	#-32768,2(a4)
	move.w	#32767,4(a4)
	move.w	#-32768,6(a4)
.Ltv_group:
	move.w	(a1)+,d1			/* i = *group++ */
	beq.w	.Ltv_done
	move.w	d1,d2
	subq.w	#2,d2
	lea	0(a0,d2.w),a5			/* node = objdat + i - 2 */
	tst.b	(a5)
	beq.w	.Ltv_group			/* node->flags == 0 */
	clr.w	(a5)				/* *pt++ = 0 */
	move.w	2(a5),d3			/* x = point.x */
	move.w	4(a5),d4			/* y = point.y */
	move.w	6(a5),d5			/* z = point.z */
	move.w	d3,d6
	muls.w	d4,d6				/* xy = x*y */
	/* MULVERTEX1(xp, m0): t0=m00+y; t1=m01+x; t2=m02*z; xp=((t0*t1+t2-xy)>>4)+m0 */
	move.w	38(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	40(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	42(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	asr.l	#4,d0
	add.l	12(sp),d0			/* + m0 */
	move.l	d0,4(sp)			/* xp */
	/* MULVERTEX1(yp, m1) */
	move.w	46(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	48(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	50(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	asr.l	#4,d0
	add.l	16(sp),d0			/* + m1 */
	move.l	d0,8(sp)			/* yp */
	/* MULVERTEX2(zp): t0=m20+y; t1=m21+x; t2=m22*z; t3=z; zp=normfx(t0*t1+t2-xy)+t3 */
	move.w	54(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	56(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	58(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	moveq	#12,d2
	asr.l	d2,d0				/* normfx */
	move.w	60(a3),d7
	ext.l	d7
	add.l	d7,d0				/* + z */
	move.l	d0,0(sp)			/* zp */
	/* sx = div16(xp, zp) + 128 */
	move.l	4(sp),d0			/* xp */
	move.l	0(sp),d2			/* zp */
	divs.w	d2,d0
	addi.w	#128,d0
	move.w	d0,8(a5)			/* vertex.x */
	/* sy = div16(yp, zp) + 128 */
	move.l	8(sp),d0			/* yp */
	divs.w	d2,d0
	addi.w	#128,d0
	move.w	d0,10(a5)			/* vertex.y */
	move.w	0(sp),d0			/* zp */
	move.w	d0,12(a5)			/* vertex.z = zp (s16) */
	/* bbox */
	move.w	8(a5),d0			/* sx */
	cmp.w	0(a4),d0
	bge.w	.Ltv_bb0
	move.w	d0,0(a4)
.Ltv_bb0:
	cmp.w	2(a4),d0
	ble.w	.Ltv_bb1
	move.w	d0,2(a4)
.Ltv_bb1:
	move.w	10(a5),d1			/* sy */
	cmp.w	4(a4),d1
	bge.w	.Ltv_bb2
	move.w	d1,4(a4)
.Ltv_bb2:
	cmp.w	6(a4),d1
	ble.w	.Ltv_bb3
	move.w	d1,6(a4)
.Ltv_bb3:
	bra.w	.Ltv_group
.Ltv_done:
	lea	20(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
```

---

## 5. Síntoma y evidencia

- Con la versión **C++**: `verify-116` PASS (balón redondo 492×494, 8 tonos, cobertura
  ~38.9 %, centroide 402,292). Todo correcto.
- Con la versión **ASM** (`K_FLATSHADE_ASM=1`):
  - **ANTES** de corregir `movea.l g_fs_args,a2` → `lea`: la CPU acababa en el **vector
    de excepción del Kickstart** (PC≈0xfc0xxx, `state=InitStarted`, frames=0). Crash.
  - **DESPUÉS** de la corrección: la demo **alcanza READY** (no crashea), pero el render
    sale **deformado**: el "balón" mide 640×400 (ratio 1.60, no redondo), cobertura
    3.5 %, centroide descentrado. Con solo el transform en ASM (face/edge en C++) el
    render sigue mal ⇒ **el bug está en `fs_transform_vertices`**.

### Método de depuración usado (por si queréis reproducir)

- **Consola del periférico de depuración en `0xB70000`**: `write byte` → consola; se lee
  con `monitor debugperiph console` por el canal lateral. Añadí marcadores ASM
  (`move.b #'F',0xb70000`) al entrar/salir de cada rutina y en puntos del bucle. Con la
  versión asm completa se veía `FfEeTp` + un bucle: face OK, edge OK, transform entra
  (`T`), preámbulo OK (`p`), y el bucle de grupos lee índices (`g`).
- **GDB** (`m68k-amiga-elf-gdb`/servidor de WinUAE-DBG): `monitor regs`,
  `disasm <addr>`, `readMemory`, watchpoints. Canal lateral: `state`, `mem`.
- **Datos leídos en runtime** (`pilka.vertexGroups`): la lista es
  `[2,16,30,44,…,828, 0, 0, …]` (60 índices, todos **pares**, paso 14 = tamaño de
  `Node3D`), luego un **0** (terminador). `objdat = _pilka_data`.

### Descartado por prueba

- **No es la división**: sustituir `divs.w d2,d0` por `moveq #0,d0` no evita el crash
  (antes del arreglo) ni cambia el render (después). (Ojo: `divs.w` excepciona por
  divisor 0 o por **overflow** del cociente s16.)
- **No es el mask del `movem`**: guardar `d0-d7/a0-a6` en vez de `d2-d7/a2-a6` tampoco.
- **No es el `lea` vs `movea.l` de los índices** (ya corregido en las 4 rutinas). El
  error ya visto fue `movea.l 0(aN,dX.w),aM` (carga el CONTENIDO) donde debía ser
  `lea 0(aN,dX.w),aM`; y `movea.l g_fs_args,a2` (carga el primer campo) donde debía ser
  `lea g_fs_args,a2`.

---

## 6. Actualización (comparación byte a byte `objdat` C++ vs ASM)

Comparando el `objdat` (los 60 nodos: flags, punto original, vértice proyectado) entre la
ruta C++ y la ASM con la **misma fase de rotación fija** (`m_angle` fijo) se encontraron y
corrigieron dos bugs más (además del `movea.l g_fs_args`):

1. **`vertex.z` mal**: en el ASM `move.l d0,0(sp)` guarda el `zp` de 32 bits; en 68000
   (big-endian) el **word alto** queda en `0(sp)` y el **bajo en `2(sp)`**. Se leía
   `move.w 0(sp),d0` (el alto → `-1`/`0xFFFF` para `zp` negativos) en vez de `move.w
   2(sp),d0`. **Corregido.**
2. (Descartado como causa) la división y el `movem`.

**Tras las correcciones, la salida del `fs_transform_vertices` ASM COINCIDE con la C++**
en todos los nodos, p. ej. con `m_angle=1000`:
`n2 v=(132,249,-3288)`, `n16 v=(171,236,-3700)`, `n44 v=(161,16,-3649)`,
`n58 v=(93,239,-3699)`, `n72 v=(132,236,-2741)` (idénticos en ambas rutas).

**PERO el render sigue negro.** Con la ruta ASM completa (face+edge+transform) y también
con **sólo el transform en ASM** (face/edge en C++), la demo alcanza READY pero la pantalla
sale toda de fondo (`verify-116`: "no hay balón"), incluso capturando una secuencia de 4
frames. **La demo NO crashea**: `g_eng_run_status.state=3` (Ready) y `frames` avanza
(34→72→109→147) mientras la pantalla está negra. Como la salida del transform coincide con
la C++, el fallo restante apunta a: (a)
`fs_update_face_visibility` / `fs_update_edge_visibility_convex` (sólo en la ruta
completa), o (b) **algún efecto colateral** del ASM (un registro que el render C++ espera,
corrupción de memoria más allá del `objdat`, o el estado del Blitter/copper). Todos los
índices de `vertexGroups` son **pares** y caen dentro de `_pilka_data`.

**Confirmaciones de una IA externa (grok) y fixes aplicados**:
- `zp` big-endian: confirmado y **corregido** (`move.w 2(sp),...` / divisor word).
- `divs.w` con divisor a 16 bits explícito: **aplicado** (`move.w 2(sp),d1; divs.w d1,d0`).
- Bucle exterior `do { while(...) } while (*group)` del C++: **aplicado**
  (`.Ltv_group_end: tst.w (a1); bne.w .Ltv_group`).
- Grok descarta `muls.w` y confirma que el layout/aritmética cuadran.
- Grok coincide en que, si el transform ya no es la causa, hay que comparar face/edge y
  revisar registros (`a6`/`d6`/`d7`) / efectos colaterales.

**Método de comparación** (reutilizable): fijar `m_angle` constante; resolver el `objdat` en
runtime vía el `.map`/magic ENG; volcar los nodos y diferenciar ambas rutas.

---

## 7. Preguntas concretas para la IA externa

1. **¿Ves algún error en el ASM de `fs_transform_vertices`** (offsets de struct, orden de
   operaciones, uso de `ext.l`/`asr.l`/`muls.w`/`divs.w`, gestión de la pila con
   `lea -20(sp),sp` y los slots `0/4/8/12/16(sp)`) que explique **vértices deformados**
   (no crash) contra la versión C++?
2. **`divs.w` con dividendo/divisor**: ¿la convención es `divs.w <ea>,Dn` con `Dn` = el
   **long** dividendo y `<ea>` el word divisor, quedando el **cociente en el word bajo**
   de `Dn`? ¿Es correcto que el C++ `div16(xp, zp)` y mi asm produzcan lo mismo si `zp`
   puede ser negativo? ¿Hay riesgo de **overflow** (p.ej. `zp` pequeño) que el C++ no
   tenga por el orden de evaluación?
3. **`muls.w d2,d1` para `m00*m01`**: el C++ hace `(s32)M.m00 * M.m01` (producto 32×32
   con extensión de signo), mi asm `muls.w` (16×16→32). Con `m00,m01` s16, ¿son
   equivalentes? ¿Puede el compilador C++ haber quedado con más precisión?
4. **`lea 0(a0,d2.w),a5` con `d2 = i - 2`**: ¿puede gas interpretar mal
   `0(a0,d2.w)` (word index, sign-extendido) frente al `objdat + (s32)(s16)(i-2)` del
   C++? Todos los índices son pares y `objdat` es par.
5. **ABI**: las rutinas hacen `movem.l d2-d7/a2-a6,-(sp)` y usan `a0/a1` como temporales
   (scratch del llamador) y `d0/d1` igual. ¿Algo más que el llamador C++ espere preservado
   y que estemos pisando? (El síntoma es render deformado, no crash, así que apunta a
   cálculo, no a corrupción de registros.)
6. **¿Alguna instrucción 68020+** que `-mcpu=68000` no aceptaría pero gas colara por
   `--register-prefix-optional`? (El objdump del ELF muestra `lea (a0,d4.w,$00)` para una
   instrucción equivalente, sin scaled index.)
7. Cualquier otra estrategia de depuración: ¿conviene comparar **byte a byte** el estado
   del `objdat` (flags/vertex de los nodes) entre la ruta C++ y la ASM para el mismo
   frame? ¿Cómo fijar la misma fase de rotación para comparar?
8. **(Preferencia de diseño) `register ... asm("dN")` en `m68k-amiga-elf-g++ 15.1.0`**:
   el original C usa variables de registro con pin
   (`register char s asm("d2"); register char flags asm("d3");`) y macros
   (`MULVERTEX1/2`, `DRAWLINE`). ¿Cuál es el mecanismo **actual** en este GCC (fork
   bebbo/amiga-gcc, `-m68000`) para que **respete** esos pins (o su equivalente) y así
   mantener el hot loop **en C++** (macros + variables de registro) en vez de un `.s`
   aparte? Concretamente:
   - ¿Sigue existiendo el soporte de `register asm("aN")`/`asm("dN")` en m68k y bajo qué
     condiciones (`-O`? `-fomit-frame-pointer`? `#pragma GCC optimize`? versión de GCC)?
     ¿Hay que usar `__asm__("d2")` o `register ... asm` con algún `-ffixed-dN`?
   - ¿Sirve `register void* p asm("a3")` o hay que usar `-ffixed-a3` + `local register
     variables` / `asm` blocks?
   - ¿Hay alguna opción (p. ej. `-fno-omit-frame-pointer` desactivado, `-fcaller-saves`,
     `-ffixed-reg`) que en la práctica recupere el codegen del original sin bajar a `.s`?
   - Si el mecanismo no existe/funciona, ¿cuál es la alternativa recomendada para
     mantenerlo en C++ (intrínsecos, `asm` inline por operación, `__builtin` )?
9. **(El bug que queda) Render negro aunque el transform coincide**: la salida del
   `fs_transform_vertices` ASM es idéntica a la C++, pero el render sale todo de fondo.
   ¿Qué puede explicarlo? Hipótesis a validar: (a) un registro que el ASM pisa y el
   render C++ espera (¿`a6`? ¿`d6`/`d7`?); (b) escritura fuera de rango de algún nodo
   (índices/vértices) que corrompe datos vecinos; (c) interacción con el Blitter/copper
   (el ASM no los toca, pero el render sí); (d) que el dump se lea en un frame distinto
   al renderizado. Se agradece una **secuencia de pasos reproducibles** para localizarlo.

**Archivos**: `support/flatshade_asm.s` (ASM), `demos/amiga/116_flatshade_convex/src/main.cpp`
(C++ canónico + llamadas), `engine/include/eng/platform/amiga/object3d.hpp` (layouts). El build
`--debug` emite además el `.s` de GCC para comparar codegen.
