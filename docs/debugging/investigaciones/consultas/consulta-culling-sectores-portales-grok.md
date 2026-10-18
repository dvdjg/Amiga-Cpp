# Consulta (Grok): culling de visibilidad para Superhot-like en A500 — sectores/portales vs BSP

**Contexto entregado:** juego estilo *Superhot* simplificado en A500 con el pipeline ya existente (`lib3d`: transformación `4.12` con `muls.w`/`divs.w`, back-face culling, painter por malla). Dato de partida: transformar 60 vértices ≈ 57k ciclos medidos; el cuello no es el orden de caras sino **transformar/rellenar lo que no se ve**. Pregunta: ¿BSP estilo Quake (con PVS) o algo más simple para indoor + algo de outdoor?

**Respuesta (resumen literal de los puntos):**

- **BSP+PVS estilo Quake es overkill**: el cuello es no transformar ni rellenar lo invisible; con 12-20 salas el flood de portales basta y un PVS sería prematuro.
- Jerarquía recomendada: **sector/portales → entidades (esfera/AABB vs frustum) → por malla back-face + painter (ya existente)**.
- Modelo: `Sector` (id, AABB/planta, mallas estáticas, entidades, portales) y `Portal` (sector destino, rect/poly del hueco, plano, flags open/closed). Flujo: localizar sector de la cámara (tracking al cruzar portal; fallback point-in-sector) → flood por portales **visibles** (plano/lado + proyección del portal a rect de pantalla + clip del frustum por ese rect, con presupuesto `max_rooms`/profundidad) → por sector alcanzado añadir estáticos y probar entidades (esfera vs frustum) → lista corta al pipeline actual.
- Outdoor ocasional: un sector `kind=Outdoor` con grid 2D o lista plana + far corto; el grafo de portales sigue siendo el dueño del indoor.
- Casos concretos: esfera vs 4-6 planos (near omitible, far sí), **far cull** por distancia² (sin `sqrt`) y **tamaño en pantalla** (`radius·focal/z < 1-2 px`). No hacer: occlusion queries, BSP con split, PVS por leaf, octree dinámico profundo.
- Puertas: portal `closed` no se atraviesa en el flood (y opcionalmente no se dibuja el hueco).
- Orden de implementación: 1) esfera-frustum sobre lista plana; 2) sectores + pertenencia; 3) portales + flood con clip por rect; 4) far/tamaño; 5) grid outdoor; 6) PVS bitset solo si el flood crece.
- Encaje: el culling corre en el **Full** (los Fakes no recalculan visibilidad); cuerpos «congelados» de otras salas ni se transforman; el cielo va al Copper (PF2), no a geometría.
- Resumen: ¿BSP? No como sistema principal en 68000 para este género. ¿Qué sí? Sectores+portales indoor, grid/lista+esfera outdoor; culling de entidad **antes** de transformar; arrays fijos sin heap.

**Mejoras incorporadas al engine al aterrizarlo** (ver `3D_GAME_ARCHITECTURE.md` §Visibilidad y `HOST-478`):

- **Vía rápida yaw-only** (`YawView`): para el género (cámaras que sólo giran en yaw) la cuña 2D + franja + rango Z evita planos normalizados y `sqrt`; rechazos conservadores con la cota `sqrt(1+W²) ≤ 1+W` (sin falsos negativos).
- **Convenio de borde**: un objeto que cruza el plano cercano se considera **visible** (sin falsos negativos); el recorte lo hacen el clipping de cámara/2D existentes.
- **Frustum general de 6 planos** con la cuña pre-escalada por `1/(1+W)` para que el test esférico genérico sea exactamente el conservador (equivalencia verificada en host); requiere escalar fraccionario (no q0).
- Métrica de éxito explícita: **objetos que nunca llaman a `transform_vertices`** (57k por 60 vértices medidos vs ~50-80 ciclos del test de culling).
- Datos: arrays de capacidad fija (`Sector`, `Portal`, entidades con `sector_id` + bounds), sin heap; PVS de sectores (bitset) solo si el flood se vuelve caro.
