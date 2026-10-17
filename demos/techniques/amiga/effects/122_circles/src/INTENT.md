# 122 - CIRCLES: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Port de `demoscene-repo-orig/effects/circles/circles.c` a la fachada de juego del engine. El original crea un bitmap de 1 plano de 320×256, configura el playfield y dibuja círculos concéntricos de radio par (`for (r = 2; r < 128; r += 2) Circle(screen, 0, 160, 128, r)`) sobre fondo negro con tinta blanca. La demo hace lo mismo con `app.screen().circle(cx, cy, r, color)`: el contexto de dibujo resuelve el mapeo del playfield, el recorte y la escritura (punto medio entero, 8 octantes, dentro del propio `Playfield`), y el juego solo describe la figura y su color. La construcción es progresiva (un radio por frame) para no meter el coste total del dibujo en un solo frame de debug; cuando termina, el frame queda estático a coste cero y el HUD muestra nombre, versión y contador de frames en la franja inferior de 8 filas (norma del repo).

## Qué pretende demostrar

1. **Que la fachada de juego basta para un efecto demoscene clásico**: el port no nombra planos, punteros, Blitter, Copper ni registros; el efecto completo son unas 20 líneas de `init`/`update`/`render` sobre `App`/`Screen`.
2. **Que una primitiva de dibujo de dominio se puede añadir sin romper el aislamiento**: `Screen::circle` se apoya en `Playfield::draw_circle` (mapeo lógico→físico + cualquier número de planos/color) y en `Surface` (clip); la demo demuestra el contrato con 1 plano y podría usarse igual en EHB o DPF.
3. **Que los recursos del engine están acotados**: el bitmap y la copperlist son reservas tipadas (`Block<Tag, Chip>`), y las escrituras de blits/BOBs validan geometría y reserva antes de emitir (`bob_detail::target_valid`/`box_fits`); ver `HOST-232`, `HOST-439` y `ROADMAP_API_COHERENCE.md` §F8.
4. **El HUD de depuración barato**: 1 bpp, texto por palabras (`Screen::text_words` → `Surface::draw_text_words`), sin bucle por píxel.

## Elementos en pantalla

- **Círculos concéntricos** blancos (color 1) sobre fondo negro (color 0), centro (160,124) y radios 2..122 en pasos de 2 (62 círculos): la misma maraña de anillos del original, recortada para no pisar la franja del HUD.
- **HUD** en la franja inferior (filas 248..255): `CIRCLES v<major>.<minor>.<build> F <frame>` con la versión inyectada por el build y el contador de frames de juego.

## Evolución temporal

- Frames 0..62 (init): cada frame añade **un** círculo de radio creciente; el dibujo se hace dentro del propio `Playfield` (una escritura de palabra por punto), así que los radios grandes cuestan unos pocos VBlanks y el resto del arranque queda por debajo. El runner espera a `mark_ready`, que se marca cuando el último radio se ha dibujado.
- Estado estable: la figura es **estática** (no se redibuja) y el coste por frame vuelve a cero; el único cambio es el contador `F` del HUD.
- Cadencia medida (`A500_debug`): **49,87 fps / 1,003 campos/frame**; `A500_release`: 49,92 fps / 1,002 campos (la construcción inicial queda fuera de la ventana de medida del arranque). El HUD se reescribe ~1 vez/segundo (coste despreciable). Sin parpadeo ni artefactos según la evaluación de visión (`VISION.md`).
