# INTENT — 001_adv_walk

## Qué hace

Ejercita el **núcleo de la capa de aventura** (`eng::adv`, hito A0) sobre contenido propio (nada del juego original): un mundo de 352×208 px con tres walkboxes convexas, un actor que camina por la ruta calculada hasta el punto pedido (click de ratón o guion determinista), una cámara de mundo con dead-zone y límites que desplaza la **ventana de display por hardware** (`BPLCON1` + punteros, convención `fine_scroll_coarse`/`fine_delay`), hotspots con etiqueta en el HUD y una franja de HUD de depuración (1 plano, texto por palabras) con nombre, versión y telemetría.

## Qué pretende demostrar

- `eng::adv::walkbox_route` (Dijkstra sobre cajas + suavizado por visibilidad) convierte un destino arbitrario en una ruta de waypoints dentro de la unión de cajas; el actor la recorre sin salirse (el destino se ajusta al borde si cae fuera).
- `eng::adv::Camera<Fixed<s16,4>>` sigue al actor con **zona muerta** (el actor se mueve ~60 px sin que la cámara se mueva) y **límites** (el origen de la vista se clampa al mundo).
- El patrón de dibujo de una aventura en OCS: fondo **estático** pintado una vez (bitmap 4bpp de 368 px, filas de 46 B con MOD al fetch de 42 B), actor y puntero como **BOBs enmascarados** (cookie-cut `$CA`, un job multi-plano) con save-under en scratch (restore → stash → draw) y HUD en su banda con MOD/`BPLCON1`/DDF propios.

## Elementos en pantalla

- **Cielo/mar/arena**: bandas de la paleta de 16 colores, espuma, muelle, cartel y pared con puerta (fondo estático que se desplaza con la cámara).
- **Actor**: figura de 16×24 (cabeza, camiseta naranja, piernas) con dos fotogramas de paso alternos.
- **Puntero**: flecha de 8×11 dibujada como bob (mismo camino de blit/save-under que el actor).
- **Hotspots**: rectángulos PUERTA (288..318×112..164) y CARTEL (118..152×138..168); con el puntero encima, el HUD muestra `MIRA: <nombre>`.
- **HUD** (banda de 48 líneas desde la 252): `001 ADV WALK`, versión con build, `FRM/CAM/WP/BF` y estado (`PASEO...`, guion, o `MIRA: ...`).

## Evolución temporal

- Al arrancar (sin input), el actor recorre un **guion determinista** en bucle: PUERTA → centro-este → CARTEL → centro-oeste → muelle-este → oeste; el contador `WP` avanza por waypoint y `FRM` por frame.
- El **click** (botón izquierdo) fija un destino nuevo en coordenadas de mundo (camara + puntero); al llegar, el guion continúa.
- La **cámara** permanece en `CAM 0` mientras el actor va dentro de la zona muerta y empieza a seguirlo (hasta el clamp en 32) al acercarse al este; el puntero se mantiene pegado a pantalla durante el paneo.
- Objetivo de presupuesto: 1 frame por VBlank (50 Hz PAL). **Estado: VERIFICADA** — ~48-49 fps estables (~1,0 campos/frame) con `measure-fps`, flicker OK y visión canónica OK; el HUD se repinta por palabras cambiadas (1-2 por frame) con `eng::playfield::DiffText` (engine, sobre la primitiva `Playfield::store_plane_words_row`), y la espera de VBlank usa sondeo activo por la carrera de despertar del bucle por señal. Ver `VALIDATION.md`.
