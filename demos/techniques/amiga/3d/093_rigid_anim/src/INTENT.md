# Intención - 093_rigid_anim

## Qué hace

Un robot de banco: torso vertical blanco con cabeza cuadrada y un brazo cian de dos segmentos (brazo y antebrazo) unido al hombro. El suelo son dos líneas horizontales estáticas. El HUD de depuración muestra nombre, versión, frames visualmente distintos, fps visual y `T`/`L`/`B` (tick, líneas y eventos).

El cuerpo y la cámara son fijos: su capa se hornea una vez en el plano 0 de los buffers. El brazo vive en el plano 1 y es lo único que se limpia, se transforma, se proyecta y se dibuja por frame. La animación la genera `eng::anim3d::Player` con un clip de 12 ticks y dos tracks.

## Qué pretende demostrar

Que el runtime de animación rígida interpola poses correctamente en Q4.12 (incluido el ratio fraccionario, que antes desbordaba), que la jerarquía de `Scene` compone hombro y codo, y que una capa estática + una capa dinámica permiten 25+ fps en A500. También demuestra un contador de **frames visualmente distintos**: si el hash del plano dinámico no cambia, el frame no sube aunque el bucle de simulación avance.

## Elementos en pantalla

- Torso rectangular blanco con cabeza cuadrada encima.
- Brazo cian de dos rectángulos con una articulación visible.
- Dos líneas de suelo blancas.
- HUD superior con versión, frame visual, fps visual y contadores.
- El contorno del brazo no debe solaparse de forma incoherente ni desaparecer.

## Evolución temporal

- El torso, la cabeza y el suelo no cambian.
- El hombro barre de `0` a `-0.35` turns y vuelve a `0` en 12 ticks; el codo acompaña con `0 → -0.20 → 0`.
- El clip hace loop y dispara dos eventos por ciclo (tick 4 y 12).
- El brazo se mantiene fuera del torso para que cada pose cambie la silueta.
- `F`/`FPS` cuentan únicamente frames cuya firma de aristas proyectadas cambia.
