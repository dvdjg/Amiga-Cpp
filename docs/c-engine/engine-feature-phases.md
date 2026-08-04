# Fases de ampliación del engine

Plan para disponer de capacidades de gameplay y gráficos avanzados sin inflar el núcleo mínimo de una sola vez.

## Fase 1 (núcleo ampliado, biblioteca actual)

Incluida en el árbol `engine/` y en `engine_suite.h`:

- Aleatorio determinista y barato (`engine_rand`).
- Entrada por flancos y comprobación de rectángulo (`engine_input_edges`); lectura de tecla mantenida (`engine_input_devices_key_held`).
- Metadatos de superficie planar interleaved (`engine_bitmap.h`).
- Aritmética fija 16.16 básica + seno por LUT (`engine_fixmath`).
- Reloj lógico en frames respecto al contador VBL (`engine_clock`).
- Listas de etiquetas para parámetros opcionales (`engine_tag.h`).
- Trazas con bloques e indentación y promedios (`engine_trace`), activo con `ENGINE_DIAG=1`.

Incluida además vía [`engine_extensions.h`](../engine/include/engine_extensions.h) (ver [engine-subsystems.md](engine-subsystems.md)):

- Copper doble buffer y primitivas WAIT/fin de lista (`engine_copper_list`).
- Sprites OCS (`engine_sprite`); joystick (`engine_joy`); view/tilemap lógicos (`engine_view`, `engine_tilemap`).
- Acceso custom centralizado (`engine_custom_chip`); fuente raster mínima (`engine_font`); DOS (`engine_dos_io`); política de audio (`engine_audio`).

## Fase 2 (playfield 2D)

Objetivo: scroll de buffer, cámara rectangular, invalidación por rectángulos. API prevista bajo prefijo `engine_playfield_*` cuando exista al menos dos consumidores (efecto + batería o dos efectos).

## Fase 3 (lotes y tiempo real)

- Lotes de objetos software con restauración de fondo (sustituto genérico de “sprites por blitter” con cola).
- Lista copper de doble buffer con bloques reordenables (efectos raster por línea).
- Audio modular (música tracker / samples) como biblioteca enlazable aparte, sin acoplar al arranque mínimo.

Cada fase debe cumplir: compilar con `scripts/verify-build.sh`, caso de batería o efecto que ejercite la API nueva, y actualizar este documento al cerrar la fase.
