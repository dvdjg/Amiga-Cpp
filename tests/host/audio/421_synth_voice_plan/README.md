# HOST-421 — planificador de voces de síntesis

Comprueba la asignación pura de pistas concurrentes a las tres voces Paula directas y las cuatro voces software del mixer. Cuando una obra necesita una octava voz, el planificador rechaza la reproducción directa o solicita el modo OctaMED si está permitido.

El test no arranca el player OctaMED ni toca hardware; verifica exclusivamente la decisión de rutas que debe preceder a la preparación de buffers y a la reproducción.
