# INTENT - 086_bob_objects

## Qué hace

Demuestra el camino de **objetos de bitmap (BOB) por Blitter** del sistema de actores (`OBJECT_SYSTEM.md`) y la gestión **por escena** del copper que cada objeto necesita: el cielo es un degradado continuo por línea (256 intenciones `PaletteLine`) y cada BOB declara sus necesidades de copper ancladas a su Y (un degradado de su color a lo largo de sus filas) que el `copper::Plan` resuelve por prioridad `(superficie, z)`. Los BOB se dibujan y borran por el Blitter con tres políticas: **cookie-cut con máscara + borrado por caja**, **OR aditivo + borrado por caja** y **opaco + save-under**, todos con desplazamiento fino (X no múltiplo de 16, con palabra de guarda). El número de BOB es configurable (`-DK_086_BOBS`, 1..16; por defecto 3).

## Qué pretende demostrar

- Que las tres políticas de borrado dejan el fondo **sin rastro** (estelas, halos o daño de restauración) sobre un fondo **estructurado** (regla 25), no liso.
- Que el copper por objeto convive con el copper de escena (cielo) sin colisiones de color: cada objeto usa su propio índice.
- Que el coste por frame cabe con el copper **estático** (`K_086_STATIC_COPPER=1`): el cielo se materializa una vez y los BOB solo mueven posiciones.

## Elementos en pantalla

- **Fondo estructurado** (pintado por CPU al init, índices 5..7 estables): suelo verde, línea de horizonte, tres montañas, sol y cinco árboles.
- **Cielo** con degradado por línea (copper, COLOR00) de azul oscuro a rojo/naranja.
- **BOB**: discos de 32×32 de 3 planos (índices 1..4) con degradado de copper propio anclado a su Y; uno por celda de una rejilla 4×2 (80×128 por celda).
- **Depuración**: sin texto en pantalla (demo anterior a la norma); la ventana de debug de WinUAE muestra el run-status (`detail = intenciones << 16 | actores << 8 | configurados`) y los contadores `fps10`, `bob_ok`, `bob_bad`.

## Evolución temporal

1. **Init**: fondo estructurado + hoja de BOB + actores + plan de copper (256 intenciones del cielo); READY.
2. **Frames**: cada BOB recorre una **lissajous con x e y** (amplitud ±18/±12 dentro de su celda) con **ritmo por objeto** (los pares al doble de velocidad); el Blitter dibuja/borra según la política y el copper por objeto (si `K_086_STATIC_COPPER=0`) se re-ancla a su Y.
3. **Cada frame**: self-check de memoria — el píxel central de cada BOB debe llevar su índice (tolerante al OR sobre fondo claro); `bob_bad` debe quedarse en 0.
