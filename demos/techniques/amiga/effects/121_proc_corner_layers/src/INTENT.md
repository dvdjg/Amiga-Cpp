# 121 — PROC STRUCTURES: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Genera un **mundo de tiles procedural EN COMPILACIÓN** (`constexpr`) y lo pinta una única vez con el **motor de tiles X-Limited** usando el Blitter (sin C2P ni composición por píxel): río N→S con lago e isla por transiciones de esquinas (dual-grid), caminos con curvas y **puente** sobre el cauce, **castillo 3×3** (muralla por casación: esquinas, torre, puerta sur, patio), **dos casas grandes 2×2** (variantes A/B con la puerta espejada), **dos árboles grandes 2×2** (copa continua + tronco), arbolitos, flores (decal) y **copas de follaje (overhead) sobre el carril** del actor. Las celdas se **deduplican** (`bake_layered_map`) y el banco de ~50 piezas únicas alimenta el relleno por Blitter. Por frame solo hay Blitter para el actor y el HUD de 1 bitplane (texto por palabras): nombre + versión + contador de frames + «X-LIMITED 20x13 PIECES».

## Qué pretende demostrar

1. La **cadena completa de assets procedurales**: generación → cocido a tileset indexado → fondo por el motor de tiles del engine (camino que usará un juego real, sin parcheo por píxel).
2. Estructuras multi-tile que **casan por construcción** (Wang por bordes y rasgos): caminos que encadenan, muralla cerrada, casas/árboles 2×2 continuos — validado en host (HOST-448) y a la vista (visión).
3. Un **actor de juego sobre el mundo corkscrew**: bicho blanco (color 31 exclusivo) que camina 1 px/frame por el carril y **pasa por debajo de las copas** (la máscara del cookie-cut compone `actor & ~copas`). El camino didáctico es el **sistema de actores del engine** (`ActorStore` + `Animation` + `SaveUnder` + pack interleaved con guarda, puente `field/actor_target.hpp`), activable con `-DK121_ACTOR_SYSTEM` (A/B en curso; el camino propio por bandas queda por defecto).
4. El **auto-chequeo de render** (patrón `COHERENCIA_DE_SETUP.md` §22): init emite y RELEE el bitmap del cuerpo (color 31), bandas/integridad y geometría, con códigos de fallo en `detail`; el bucle relee un píxel del cuerpo por frame (3 strikes).

## Elementos en pantalla

- **Franja superior ~208 px (13 filas de tile, 32 colores del atlas)**:
  - Agua con orillas orgánicas: río diagonal en la mitad izquierda y lago con isla a la derecha; espuma clara en las transiciones.
  - **Caminos** de tierra: recta horizontal por el centro-oeste que cruza el cauce por un **puente**, subida y codo hacia la puerta sur del **castillo** (arriba-derecha, muralla de piedra con torre).
  - **Casas grandes 2×2** marrones (tejado, ventanas de mármol, puerta) abajo-izquierda (variante A puerta izquierda junto al río; variante B puerta derecha junto al lago) y **árboles** de copa verde (dos grandes 2×2 arriba y arbolitos/flores sueltos).
  - **El actor**: criatura blanca de 16×16 con **dos ojos oscuros**, sobre el carril horizontal (fila 9); al llegar a la zona del lago, el carril cruza agua.
  - **Copas de follaje** (verde oscuro) sobre el carril en dos zonas: par de copas a la izquierda del centro y una copa aislada más a la derecha; el bicho **desaparece debajo de ellas** y reaparece al salir.
- **Franja inferior (HUD, 1 bitplane, blanco/negro)**: «PROC STRUCTURES vX.Y.NNNN F n» (versión y contador de frames) y «X-LIMITED 20x13 PIECES» (geometría y nº de piezas cocidas).

## Evolución temporal

- Cámara **fija**: el mundo no scrollea; solo cambian el actor y el contador del HUD.
- El actor avanza **1 píxel por frame** (50 Hz) en línea recta con **rebote** en los extremos (deja de ir a la derecha y vuelve): entra en las copas (queda total o parcialmente oculto), sale, cruza el centro y las casas, y rebota.
- El contador «F n» del HUD se actualiza cada 64 frames.
- La oclusión bajo copa debe ser **progresiva** (el borde de la silueta de la copa tapa la parte correspondiente del cuerpo) y sin parpadeo ni residuo del frame anterior.

## Estado y limitaciones conocidas

- El número de fps y las medidas de cuadre están en `VALIDATION.md`; la deuda abierta en `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md` (DT-022 cerrado en la demo; DT-023 herramientas; DT-024 migración al sistema de actores: cerrado — sistema por defecto y golden 16/16).
- **Pendiente de verificación (hallazgo 2026-10-16)**: con medición por landmarks del mundo (copas) sobre capturas, el actor aparece una columna (16 px) a la izquierda de su x nominal respecto al mundo — la guarda por frame no lo ve (comparte el marco de dibujo) y se detectó con medida relativa (ver el informe `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md`). El arreglo (guarda X espejo de la vertical, `kRingGuardX`, aplicada a dibujos Y lecturas) está aplicado en el código y **pendiente de verificación de captura**: las capturas del runner con el actor estático salen sin actor aunque `detail` confirma que el update corre con la x correcta — la captura no refleja el framebuffer (investigación abierta, ver DT-023).
