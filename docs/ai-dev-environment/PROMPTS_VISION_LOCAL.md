# Prompts para modelos de visión local (Ollama)

Registro de referencia de **cómo pedir** a los modelos de visión locales (Qwen3-VL, Gemma 3) para verificar demos y capturas del engine. Recoge los principios, las eventualidades a buscar y plantillas listas por escenario. Los prompts **ejecutables** viven en los ficheros de cada herramienta (ver [§5](#5-relación)); este documento es la referencia para escribirlos y revisarlos.

## 1. Principios: por qué los VLM fallan y qué hacer

| Principio | Regla |
|---|---|
| **Recortar y preguntar por zonas** | Nunca preguntar «varias cosas» sobre una imagen completa: el modelo atiende a lo dominante y se pierde el resto. **Recortar** la región de interés (una franja, un panel, un objeto) y preguntar **solo** por ella. |
| **Expectativa explícita** | Decir exactamente qué *debería* verse (colores, número de elementos, forma, posición relativa) y pedir **confirmar o descartar**, no describir libremente. |
| **Una pregunta por imagen** | Varias preguntas compuestas → respuestas parciales o alucinadas. Una imagen, una cosa. |
| **Sin coordenadas de píxel** | Pedir píxeles (`y=715`) produce alucinaciones. Usar regiones **relativas** (mitad izquierda, tercio superior, la primera columna de 16 px). |
| **Formato fijo** | `sí/no` + tipo + zona + confianza + 1–2 frases. Evita respuestas vagas. |
| **Contexto de dominio** | Indicar «Amiga 500, lowres, paleta limitada» para que el *banding* o la paleta reducida no se lean como fallo. |
| **Referencia primero** | En secuencias, dar un frame **de referencia** (sano) y comparar contra él; describir frames aislados es donde más se falla. |
| **Determinista > visión** | La capa determinista (diff de píxeles, optical flow) decide *dónde* mirar; el VLM solo **confirma o descarta**. Ante discrepancia, prevalece lo determinista. |
| **Modelo adecuado** | Qwen3-VL (principal, secuencias); Gemma 3 (alternativa en imagen estática). Evitar LLaVA clásico (el que más alucina). |

## 2. Eventualidades que hay que saber detectar

No se meten todas en un prompt: sirven para **elegir la pregunta** adecuada a lo que se está validando.

- **Color**: inversión (dos colores intercambiados), paleta equivocada, *banding* no intencionado, color de fondo erróneo, `COLOR0` visible donde no toca.
- **Presencia**: elemento que falta, duplicado, **camuflado con el fondo**, recortado por el borde, con el color del fondo.
- **Prioridad y oclusión**: un sprite tapado por otro cuando debería ir **delante** (o al revés); el número de elementos visibles no coincide con el esperado.
- **Geometría**: figuras cortadas o desplazadas, **huecos/bandas vacías**, saltos de 1 tile/píxel, ensamblado incorrecto de un scroll, anillos/patrones que no encajan.
- **Temporal**: parpadeo (*flicker*), *tearing*, aparición/desaparición injustificada, oscilación A-B-A entre frames.
- **Texto e interfaz**: dígitos o *glyphs* cortados, ilegibles o mal alineados.
- **Movimiento**: dirección y velocidad, suavidad, «trompicones», continuidad entre frames.
- **Separación entre zonas**: filas o columnas frontera con píxeles sueltos, puntos de un color que no debería estar ahí.

## 3. Plantillas por escenario

### 3.1 Descripción general (uso limitado)

Útil solo como primera ojeada. El modelo tiende a describir lo dominante y a omitir el resto; para verificar, pasar a §3.2.

```text
Eres un analizador de capturas de una demo de Amiga 500 (lowres, paleta limitada).
Describe con precisión: fondo, formas/objetos (bandas, sprites, tiles, texto) y su
posición relativa. Señala cualquier anomalía (negro interno, tearing, bandas
incorrectas, corrupción de color o geometría). Breve (máx 140 palabras).
```

### 3.2 Por regiones/recorte (RECOMENDADO)

**Recortar** cada zona (franja, panel, columna…) y preguntar por una a la vez, diciendo qué debe verse en esa zona concreta.

```text
Esta imagen es UNA sola franja horizontal (320 px) de una demo de Amiga 500.
Debería verse: <p. ej. fondo azul arriba, suelo marrón abajo con montañas amarillas>.
Responde SOLO: ¿coincide con esa descripción? Si algo no cuadra, dime qué y dónde
(izquierda/centro/derecha, arriba/abajo). Sin coordenadas numéricas. Breve.
```

Regla práctica: un recorte por zona y una expectativa por recorte. Si hay tres franjas, tres recortes y tres preguntas.

### 3.3 Confirmar una expectativa concreta (sí/no)

La forma más fiable: convertir «lo que quiero» en una afirmación y pedir confirmación.

```text
Afirmación a verificar: «en la franja del medio se ven exactamente dos formas sueltas,
una roja y una blanca, por delante del fondo».
Responde SOLO: correcto / incorrecto. Si es incorrecto, qué se ve en su lugar. Breve.
```

### 3.4 Comparación A/B (referencia vs candidato)

```text
Frame A = referencia (comportamiento correcto). Frame B = el que evalúo.
Compáralos y dime SOLO en qué se diferencian de forma relevante (color, posición,
elementos que aparecen/desaparecen), en lenguaje relativo. Sin coordenadas. Breve.
```

### 3.5 Secuencia y movimiento

Se envían 3–5 frames consecutivos. No se describe cada uno: se pregunta por la **dinámica**.

```text
Son N frames CONSECUTIVOS de la misma escena (Amiga 500, lowres).
¿El contenido se mueve de forma suave y coherente (sin saltos, sin parpadeos,
sin elementos que aparezcan/desaparezcan sin motivo)? Responde SOLO:
- Movimiento: suave / a saltos / parpadea
- Sentido: <izquierda/derecha/arriba/abajo/estático>
- Anomalía: sí/no + tipo + zona relativa
Breve.
```

### 3.6 Parpadeo / glitch / tearing

Ver el contrato completo en [`tools/vision-review/PROMPTS.md`](../../tools/vision-review/PROMPTS.md) (prompt estructurado con la zona candidata que fija la capa determinista y ejemplos *few-shot* para parpadeos conocidos).

### 3.7 Color (inversión y paleta)

```text
Esta región debería tener el color X arriba y el color Y abajo
(p. ej. suelo marrón, picos amarillos). ¿Los colores son correctos o están
intercambiados/invertidos? Responde SOLO: correcto / invertido / otro, y descríbelo. Breve.
```

Antídoto contra el camuflaje: comparar el color del elemento con el del fondo en la **misma** región.

### 3.8 Sprites (número, prioridad, camuflaje)

```text
En esta región debería haber <N> formas de sprite, de colores <…>, y deberían verse
POR DELANTE del fondo. ¿Cuántas formas ves, de qué colores, y están delante o
tapadas por el fondo? Sin coordenadas. Breve.
```

### 3.9 Tiles y scroll

```text
Esta es una tira de un fondo con scroll. Los tiles deberían ensamblarse sin cortes
ni huecos y desplazarse de forma continua. ¿Ves algún corte, hueco (banda vacía),
o salto de un tile entre zonas? Responde con la zona relativa. Breve.
```

### 3.10 Texto y dígitos

```text
Recorte de un marcador de texto. Dime qué números o letras se leen, y si algún
carácter está cortado o es ilegible. Breve.
```

## 4. Buenas prácticas de ejecución

- **Temperatura 0** y salida acotada; reintentar si el JSON no se puede parsear.
- **No enviar vídeo crudo** al VLM: extraer frames (orden y telemetría conservados).
- Ante respuestas contradictorias, **repetir el recorte más pequeño** y la pregunta más específica.
- Un solo recorte + una sola pregunta >> una imagen completa con varias preguntas.
- Guardar la respuesta cruda y el recorte enviado (evidencia reproducible).

## 5. Relación

- Guía de Ollama local (modelos, arranque, endpoints): [`ollama-local.md`](ollama-local.md).
- Prompts ejecutables de revisión de visión (flicker/glitch, híbrido determinista→visión): [`tools/vision-review/PROMPTS.md`](../../tools/vision-review/PROMPTS.md).
- Prompts ejecutables de perfiles: [`tools/profile/prompts/`](../../tools/profile/prompts/) (`generic.md`, `020-copper-basic.md`, `050-blitter-bobs.md`).
- Metodología de depuración visual de demos: [`DEMO_VISUAL_DEBUG.md`](../guides/methodology/DEMO_VISUAL_DEBUG.md).
