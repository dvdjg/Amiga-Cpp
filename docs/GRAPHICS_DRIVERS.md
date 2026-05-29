# Drivers graficos

Un driver grafico es una estrategia de composicion para un hardware concreto. No es
solo un modo de pantalla: tambien define presupuestos, memoria, reglas de paleta,
uso de sprites, copper, blitter y restricciones artisticas.

La logica de juego debe expresar intenciones:

- mostrar actor;
- mover camara;
- activar hotspot;
- cambiar room;
- pedir efecto de luz/agua;
- mostrar dialogo.

El driver decide como materializar esas intenciones en el hardware disponible.

## Drivers iniciales

### `EhbScene`

Primer objetivo serio. Pensado para aventura grafica moderna:

- 6 bitplanes EHB;
- fondos ricos de 32 colores base + 32 half-brite;
- pocos BOBs grandes;
- cursor o detalles con sprites hardware;
- copper por zonas y efectos baratos;
- cambios completos de paleta solo en zonas seguras o transiciones.

### `Standard5`

Modo de 5 bitplanes para 32 colores reales. Menos costoso que EHB y util para
escenas con mas movimiento.

### `Standard4`

Modo de 4 bitplanes para 16 colores. Base recomendable para accion rapida,
muchos BOBs o scroll exigente.

### `FakeDualPlayfield`

Composicion estilo "DPF falso": por ejemplo 4 planos para foreground y 1 plano
para fondo, sombra, mascara o decoracion. Inspirado en trucos como los usados en
juegos que aparentan mas capas sin activar Dual Playfield real.

### `DualPlayfield`

DPF real OCS con PF1/PF2, scroll independiente y parallax. Es el driver para
escenas tipo Mega Typhoon o Jim Power, con el coste artistico de repartir colores
por playfield.

### `SpriteBackdrop`

Uso de sprites hardware como parte del fondo: tiras, multiplexado vertical,
repeticion horizontal y fondos asistidos por copper.

### `CopperHeavy`

Escenas especiales con fuerte carga copper: intros, menus, fondos raster,
transiciones, cielos, agua o efectos demoscene.

## Regla de oro

Si una escena necesita romper demasiadas reglas de un driver, probablemente no
necesita mas excepciones: necesita otro driver.

