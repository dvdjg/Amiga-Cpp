# Perfil psicológico de NPC (`eng::sim`, capa de persona)

`engine/include/eng/sim/` describe criaturas con necesidades, personalidad, afecto, memoria, relaciones y sociedad. Esta capa añade el **perfil psicológico de un personaje controlado por la máquina** —arquetipo, rasgos, aptitudes, defectos y **expresión no verbal**— y el modo en que los demás lo **leen**. Es transversal al engine: el primer consumidor es `eng::cards` (póker), pero la maquinaria sirve a cualquier juego con personajes.

El objetivo es un **ambiente creíble**: que un personaje no solo decida bien o mal, sino que **se le note** (o no) lo que siente, que los rivales **aprendan** a leerlo con el tiempo, y que su estado psicológico **evolucione** durante la partida. El sistema se apoya en lo que ya existe y no lo duplica: `Personality`, `Mind`/`Emotions`, `Relationship`, `MemoryKind`, `Senses`, `Tracker`, `behavior.hpp`, `communication.hpp` y `body.hpp`.

Referencias de diseño: los rasgos y la utilidad de *Rain World* (`docs/engine/architecture/SIM_ECOSYSTEM.md` §4–§5) y la taxonomía de decisión de `docs/engine/architecture/GAME_AI_LIBRARY.md`. Plan por fases: [ROADMAP_NPC_PSYCHOLOGY.md](../../guides/roadmap/ROADMAP_NPC_PSYCHOLOGY.md).

## 1. Alcance y relación con `eng::sim`

La capa de persona se divide en cinco piezas, todas header-only, enteras, sin heap y deterministas, en `engine/include/eng/sim/`:

```text
   persona.hpp      → arquetipo + rasgos sociales/mentales + aptitudes + defectos
   expression.hpp   → canales y gestos (microexpresiones) y su intensidad
   psyche.hpp       → estado psicológico temporal (tensión, racha, tilt, compostura)
   read.hpp         → modelo que un observador construye sobre un rival (tells)
   convention.hpp   → señales secretas pactadas (base del Mus)
```

```text
   PERSONALIDAD (estable)        ESTADO (temporal)            EXPRESIÓN (por frame/turno)
   ┌──────────────────┐          ┌──────────────────┐         ┌───────────────────────┐
   │ rasgos base (10) │          │ compostura       │         │ canal × gesto × intens│
   │ rasgos psique(20)│  ─────►  │ tilt, racha,     │ ──────► │ fugas (leaks)         │
   │ aptitudes        │          │ fatiga, ánimo    │         │ control 0..100        │
   │ defectos         │          │ Mind (afectos)   │         └───────────┬───────────┘
   └──────────────────┘          └──────────────────┘                     │
            ▲                             ▲                               ▼
            │      eventos de la partida (ganar/perder/farol cazado)   OBSERVADORES
            └─────────────────────────────┴──────────────────────►  read.hpp (creencias)
```

Lo que **ya existe** y se reutiliza tal cual:

| Necesidad | Primitiva existente (`eng::sim`) |
|---|---|
| Rasgos de personalidad base | `Personality` (10 rasgos `u8` `[0,100]`) |
| Afecto (12 ejes) y pulsiones | `Mind`, `Emotions`, `Emotion`, `AffectParams` |
| Memoria episódica y valencia | `MemoryEvent`, `MemoryKind`, `remember`, `attitude_toward` |
| Vínculo dirigido | `Relationship`, `bond_score` |
| Percepción y atención | `Senses`, `Tracker`, `attention_score`, `best_attention_tracker` |
| Señales (voz/gesto) | `communication.hpp` (`SignalKind`, `make_signal`, `receive_signals`) |
| Postura corporal | `body.hpp` (`BodyPose`, `pose_from_behavior`, `pose_from_state`) |
| Decisión | `behavior.hpp` (utilidad, pesos, histéresis) |

Lo que **añade** esta capa: rasgos de segundo bloque (control, engaño, suspicacia…), **aptitudes** (pericia), **defectos**, la **compostura** y las **fugas expresivas**, el **modelo de lectura** de rivales y las **convenciones secretas**. Nada de esto existe hoy en `eng::sim`: hoy la comunicación emite señales derivadas de la conducta y el afecto (`signal_for_behavior`), pero **no modela el control de la expresión** ni la **inferencia** que otros hacen sobre ella.

## 2. Modelo en tres planos

La psicología de un personaje se lee en tres planos que cambian a escalas de tiempo distintas:

- **Personalidad (estable)**: rasgos `u8` que no cambian durante la partida (como mucho, evolucionan entre sesiones). Definen *quién es*.
- **Estado (temporal)**: `Mind` (afectos) más un `PsycheState` nuevo (compostura, tensión, racha, tilt, fatiga, ánimo, imagen de mesa). Cambia por turno y por mano.
- **Expresión (instantánea)**: qué gesto se filtra en un momento dado, en qué canal y con qué intensidad. Se calcula de la personalidad y el estado.

La clave es que la expresión **no es aleatoria**: es la **fuga** de un estado interno que el personaje intenta (o no) controlar. Un mismo estado produce fugas distintas según la compostura y la dificultad de control de cada canal.

## 3. Rasgos: catálogo

Los rasgos se guardan como `u8` en `[0, 100]` (50 = neutro), igual que `Personality`, y se convierten a modificador con `trait_mod` y `apply_mod` (ya existentes). Se agrupan en dimensiones.

### 3.1 Rasgos base (existentes, `Personality`)

Supervivencia: `bravery`, `nervousness`, `curiosity`. Conflicto: `aggression`, `dominance`, `autonomy`. Vínculo: `sociability`, `empathy`, `loyalty`. Trabajo: `diligence`.

### 3.2 Rasgos de psique (nuevos, `PsycheTraits`)

Veinte rasgos nuevos, agrupados por función. Ninguno es un arquetipo: los arquetipos (§4) son **combinaciones** de estos rasgos.

**Control de sí mismo**

| Rasgo | Significado | Alto | Bajo |
|---|---|---|---|
| `composure` | autocontrol de la expresión y las emociones | cara de póker | se le nota todo |
| `concentration` | atención sostenida a la mesa | observa y recuerda | se distrae |
| `patience` | tolerancia a esperar sin jugar | roca | se aburre, juega basura |
| `temper` | irascibilidad (se enciende) | irascible | flema |
| `impulsivity` | actuar antes de pensar | temerario | reflexivo |

**Vida social**

| Rasgo | Significado | Alto | Bajo |
|---|---|---|---|
| `deceit` | habilidad y gusto por el engaño/farol | embustero | transparente |
| `gullibility` | credulidad ante el engaño ajeno | pardillo | desconfiado |
| `suspicion` | tendencia a sospechar de todos | paranoico | confiado |
| `honesty` | aversión a mentir | sincero | tramposo |
| `generosity` | gusto por dar/compartir | generoso | avaro |
| `vanity` | ego, necesidad de admiración | engreído | humilde |
| `morality` | bondad frente a maldad | sabio/bueno | malo |
| `humor` | sentido del humor y ganas de broma | chistoso | seco |
| `talkativeness` | ganas de hablar/comentar | charlatán | callado |
| `self_esteem` | valoración de sí mismo | seguro | acomplejado |

**Cognición y creencias**

| Rasgo | Significado | Alto | Bajo |
|---|---|---|---|
| `attention` | observación de los demás (detectar tells) | lee a todos | no ve nada |
| `social_memory` | recordar el historial de cada rival | memorión | olvidadizo |
| `superstition` | atribuir suerte a amuletos/rituales | cenizo/místico | racional |
| `risk` | tolerancia al riesgo y a la varianza | temerario | conservador |
| `optimism` | expectativa positiva del resultado | ilusionado | cenizo |

### 3.3 Aptitudes (pericia adquirible, `Skills`)

A diferencia de los rasgos, las **aptitudes** miden *cuánto sabe hacer* el personaje y **mejoran o empeoran** con la experiencia (la pericia no es carácter). Se guardan igual, `u8` `[0,100]`.

| Aptitud | Qué mide |
|---|---|
| `odds_math` | calcular probabilidades y pot odds |
| `hand_selection` | elegir con qué manos entrar |
| `bet_sizing` | elegir tamaños de apuesta |
| `bluffing` | ejecutar faroles creíbles |
| `trapping` | tender trampas (slowplay) |
| `opponent_reading` | leer tells y patrones del rival |
| `tell_control` | ocultar las propias fugas (refuerza `composure`) |
| `deceit_detection` | detectar que le mienten |
| `position_awareness` | jugar la posición |
| `bankroll` | gestión de la banca y del stack |
| `history_memory` | recordar acciones concretas de rivales |
| `adaptation` | cambiar de plan cuando el rival se adapta |
| `tilt_resistance` | aguantar la mala racha sin descontrolarse |
| `table_presence` | imponer respeto en la mesa |
| `endurance` | rendir igual al final de una sesión larga |

### 3.4 Defectos y limitaciones (`Flaws`)

Los defectos son **condiciones** que activan sesgos cuando concurren (no rasgos fijos). Cada uno tiene un disparador y un efecto.

| Defecto | Disparador | Efecto |
|---|---|---|
| `tilt` | pérdidas seguidas o mala jugada | sube agresión y riesgo, baja compostura y paciencia |
| `chasing_losses` | va perdiendo | juega manos peores para recuperar |
| `overconfidence` | racha ganadora | sobreestima su mano, farolea de más |
| `fear_of_loss` | stack corto | juega asustado, se retira de más |
| `greed` | bote grande | paga de más por codicia |
| `boredom` | muchos turnos sin jugar | entra con basura |
| `distraction` | fatiga, entorno | pierde tells y comete errores |
| `drowsiness` | sesión larga | baja concentración y odds_math |
| `inebriation` | (opcional) | sube sociabilidad y riesgo, baja control |
| `paranoia` | derrotas | ve trampas donde no las hay |
| `stubbornness` | discusión previa | no cambia de estrategia |
| `denial` | error propio | no aprende de sus fallos |
| `analysis_paralysis` | decisión difícil | tarda en exceso (tell de tiempo) |
| `copycat` | rival con éxito | imita sin entender |
| `predictability` | rutina | patrón de apuestas explotable |
| `venganza` | rival le ganó un bote | persigue a ese rival |

### 3.5 Estado temporal (`PsycheState`)

Campos `u8` que cambian durante la partida y modulan las fugas y la decisión:

| Campo | Significado | Efecto |
|---|---|---|
| `composure_now` | compostura efectiva (base + fatiga − tilt) | cuánto se filtra |
| `tension` | activación general (nervios) | pestañeo, temblor, sudor |
| `confidence` | seguridad actual | agresión, farol, tamaño |
| `tilt` | descontrol acumulado | fugas, agresión, errores |
| `fatigue` | cansancio | concentración, odds_math |
| `mood` | ánimo del día | optimismo/pesimismo |
| `streak` | racha (positiva o negativa) | confianza y tilt |
| `table_image` | cómo cree que lo ven | ajusta farol y disimulo |
| `time_pressure` | prisa (reloj) | impulsividad |

## 4. Arquetipos: catálogo de personalidades

Un **arquetipo** (`Archetype`) es una plantilla de rasgos y defectos con nombre. No es un `enum` cerrado: es una fila de la tabla `kArchetypes[]` que produce una `Persona` concreta al `materialize` (con jitter determinista, igual que `PersonalityTemplate`). Dos personajes del mismo arquetipo difieren por el ruido y por su historia.

La tabla siguiente es el **catálogo de arquetipos** pedido. Cada fila indica los rasgos dominantes (abreviados), los defectos típicos, los gestos que suele **filtrar** y su efecto en la mesa de póker.

| Arquetipo | Rasgos dominantes | Defectos típicos | Tells/gestos típicos | Efecto en póker |
|---|---|---|---|---|
| **Flemático** | composure↑, temper↓, patience↑, impulsivity↓ | — | casi inmóvil; mirada fija; sin tics | difícil de leer; juego estable |
| **Pardillo** | gullibility↑, composure↓, attention↓, honesty↑ | overconfidence, copycat | se le escapan sonrisas; ojos muy abiertos | farolea mal y se le nota; pagará de más |
| **Embustero** | deceit↑, composure↑, suspicion↑, morality↓ | overconfidence | falsos tells deliberados; relamerse | farol creíble; juega al despiste |
| **Experimentado** | composure↑, attention↑, social_memory↑, odds_math↑ | predictability | mínimos; rutina controlada | lee y ajusta; explotable a largo plazo |
| **Indeciso** | patience↓, impulsivity↑, self_esteem↓ | analysis_paralysis, fear_of_loss | duda, mira las cartas repetido, tarda | apuesta débil; se le ve la duda |
| **Listillo** | deceit↑, attention↑, vanity↑, suspicion↑ | overconfidence | muecas al rival; comentarios | farol y contra-farol; puede cazarte |
| **Bobo** | attention↓, gullibility↑, concentration↓ | copycat, boredom | distraído, mira alrededor | imprevisible sin querer; fácil de engañar |
| **Engreído** | vanity↑, dominance↑, empathy↓, composure↑, deceit↑ | overconfidence, predictability | mentón alto, sonrisa de superioridad | juega para lucirse; se le pilla el farol |
| **Paternalista** | empathy↑, generosity↑, dominance↑, morality↑ | — | asiente, aconseja, gesto de proteger | juega protector; subestima al rival |
| **Condescendiente** | vanity↑, empathy↓, dominance↑, temper↓ | overconfidence | mirada de lástima, media sonrisa | infravalora; paga por orgullo |
| **Idiota** | attention↓, concentration↓, gullibility↑, impulsivity↑ | chasing_losses, denial | ríe sin motivo, habla solo | decisiones sin sentido; mina la mesa |
| **Sabio** | morality↑, attention↑, patience↑, composure↑ | — | sereno; pocos gestos; mira a los ojos | casi imposible de leer; juega fino |
| **Ignorante** | attention↓, odds_math↓, patience↓ | boredom, copycat | preguntas, confusión, encogerse | no calcula; fácil de explotar |
| **Malo** | morality↓, deceit↑, aggression↑, empathy↓ | venganza, greed | mirada dura, mandíbula tensa | juega sucio; puede intimidar |
| **Envidioso** | vanidad↑, empatía↓, rencor↑ | chasing_losses, venganza | mira los stacks ajenos; resopla | persigue al que va ganando |
| **Intolerante** | temper↑, empathy↓, patience↓ | paranoia, stubbornness | resopla, comenta en voz alta | se descontrola con según quién |
| **Amigable** | sociability↑, empathy↑, generosity↑, honesty↑ | — | sonríe, bromea, charla | ambiente distendido; juega blando |
| **Cenizo** | optimism↓, superstition↑, sadness↑ | denial, fear_of_loss | suspiros, cabeza baja, queja | ve mala suerte; se retira pronto |
| **Tristón** | sadness↑, optimism↓, sociability↓ | apathy, fear_of_loss | mirada baja, hombros caídos | juega pasivo y sin ganas |
| **Alegre** | optimism↑, sociability↑, humor↑ | overconfidence, boredom | risa fácil, gesticula | sube el ritmo; farolea por diversión |
| **Chistoso** | humor↑, talkativeness↑, sociability↑ | distraction, boredom | bromas, muecas, imita | distrae a la mesa; tell ruidoso |
| **Impertinente** | temper↑, talkativeness↑, empathy↓ | stubbornness | interrumpe, señala, provoca | busca el pique; se le ve la ira |
| **Despistado** | attention↓, concentration↓, social_memory↓ | distraction, boredom | mira fuera, tarda en enterarse | pierde tells y pagos |
| **Esperanzador** | optimism↑, patience↑, morality↑ | — | sonríe al futuro; alienta | confía en mejorar; paga con fe |
| **Ilusionado** | optimism↑, risk↑, curiosity↑ | overconfidence, chasing_losses | ojos brillantes, se inclina | se emociona; apuesta de más |
| **Enamorado** | love↑, empathy↑, attention↓ | distraction, fear_of_loss | mirada perdida, sonríe solo | distraído; regala fichas al "ser querido" |
| **Irascible** | temper↑, aggression↑, composure↓ | tilt, venganza | puños, golpes en la mesa, resopla | se le ve todo; entra en tilt fácil |

Arquetipos adicionales útiles, del mismo mecanismo: **Timorato** (nervousness↑, risk↓), **Temerario** (risk↑, impulsivity↑), **Avaro** (generosity↓, greed), **Generoso** (generosity↑), **Vengativo** (venganza, rencor↑), **Fatalista** (optimism↓, superstition↑), **Sanguíneo** (sociability↑, temper↑, impulsivity↑), **Metódico** (concentration↑, patience↑, diligence↑), **Místico** (superstition↑), **Nostálgico** (social_memory↑, sadness↑), **Tramposo** (honesty↓, deceit↑, composure↑), **Inocente** (gullibility↑, honesty↑).

El **Engreído** ilustra la idea de que un arquetipo arrastra rasgos coherentes: prepotente (`dominance`↑), con aires de superioridad y vanidoso (`vanity`↑), narcisista (`empathy`↓, `self_esteem`↑), flemático (`composure`↑, `temper`↓) y con tendencias sociopáticas (`morality`↓, `empathy`↓). Esa constelación se declara en su fila, no en código especial.

## 5. Expresión no verbal: canales y gestos

La expresión se organiza en **canales** y **gestos**. Cada gesto tiene un **coste de control** (`control`, 0 = imposible de ocultar, 100 = se controla siempre) y una **detectabilidad** (`detect`, cuánto cuesta verlo). Un personaje con compostura alta filtra sobre todo por los canales de control bajo.

```cpp
enum class ExpressionChannel : u8 { FaceEyes, FaceMouth, FaceBrow, Body, Posture, Gaze, Voice, Timing, Count };
enum class GestureKind : u8 { /* ... catálogo ... */ };
```

Catálogo de gestos pedido, con su canal y su dificultad de control (0 = se escapa siempre; 100 = siempre disimulable):

**Ojos y cejas (canal `FaceEyes`/`FaceBrow`)**

| Gesto | Control | Se lee como |
|---|---|---|
| `blink` (pestañeo normal) | 10 | — |
| `blink_fast` (pestañeo rápido) | 20 | nervios, tensión |
| `blink_hold` (cerrar y aguantar) | 60 | contención, decisión |
| `pupil_dilate` | 5 | interés, sorpresa (casi imposible de fingir) |
| `wide_eyes` | 25 | sorpresa, mano fuerte |
| `squint` | 40 | duda, desconfianza |
| `brow_raise` | 35 | interés, farol o saludo |
| `brow_furrow` | 30 | concentración, enfado |
| `forehead_tension` | 20 | tensión contenida |
| `eye_roll` | 70 | desprecio |
| `stare_down` | 75 | desafío |
| `gaze_aversion` | 45 | evitación, mentira |

**Boca y nariz (canal `FaceMouth`)**

| Gesto | Control | Se lee como |
|---|---|---|
| `smile` | 60 | alegría, cordialidad |
| `smirk` | 55 | superioridad, sorna |
| `grimace` | 25 | disgusto, mano mala |
| `lip_corner_twitch` | 15 | emoción contenida (microexpresión) |
| `lip_press` | 40 | contención, enfado |
| `lip_bite` | 30 | duda, nervios |
| `lick_lips` | 25 | nervios, ansiedad |
| `swallow` | 20 | tensión, mentira |
| `tongue_out` | 50 | burla, relajo |
| `jaw_drop` | 20 | sorpresa |
| `nose_flare` | 15 | ira, excitación (autonómico) |
| `nostril_twitch` | 15 | desprecio, tensión |

**Cuerpo y postura (canales `Body`/`Posture`)**

| Gesto | Control | Se lee como |
|---|---|---|
| `shoulder_tension` | 30 | tensión |
| `shrug` | 70 | indiferencia fingida |
| `head_tilt` | 65 | interés, duda |
| `head_nod` | 70 | asentimiento |
| `head_down` | 45 | sumisión, tristeza |
| `neck_scratch` | 50 | duda, incomodidad |
| `ear_scratch` | 50 | picor nervioso |
| `arm_scratch` | 55 | picor nervioso |
| `hand_tremor` | 10 | miedo, excitación (autonómico) |
| `finger_tap` | 35 | impaciencia, nervios |
| `fidget` | 30 | inquietud |
| `leg_bounce` | 20 | piernas inquietas, ansiedad |
| `foot_tap` | 25 | impaciencia |
| `lean_in` | 60 | interés, ataque |
| `lean_back` | 60 | retirada, desdén |
| `cross_arms` | 55 | defensa, cierre |
| `open_posture` | 65 | confianza |
| `slump` | 35 | desánimo |
| `chest_puff` | 70 | fanfarronería |
| `fist_clench` | 25 | ira contenida |
| `wipe_palms` | 20 | sudor, tensión |
| `adjust_clothes` | 45 | incomodidad |
| `touch_face` | 35 | auto-contacto, nervios |
| `cover_mouth` | 40 | ocultar (mentira o risa) |
| `self_hug` | 30 | inseguridad, frío |

**Voz y paraverbal (canal `Voice`)**

| Gesto | Control | Se lee como |
|---|---|---|
| `sigh` | 40 | resignación, cansancio |
| `cough` | 60 | carraspeo, tensión |
| `throat_clear` | 55 | nervios |
| `laugh` | 60 | alegría |
| `nervous_laugh` | 30 | tensión |
| `whistle` | 65 | calma fingida |
| `hum` | 65 | distracción, calma |
| `breath_hold` | 25 | expectación |
| `speech_rate_fast` | 35 | nervios |
| `silence` | 50 | contención (a veces, fuerza) |

**Tiempo y apuesta (canal `Timing`, tells conductuales)**

| Gesto | Control | Se lee como |
|---|---|---|
| `tank` (pensar mucho) | 45 | decisión difícil |
| `instant_call` | 35 | mano hecha o farol |
| `instant_fold` | 40 | mano débil |
| `instant_raise` | 30 | fuerza o fanfarronada |
| `overbet` | 50 | presión, farol o valor |
| `min_bet` | 55 | cebo |
| `check_dark` | 60 | farol o trampa |
| `chip_fumble` | 20 | nervios |
| `bet_rhythm` | 40 | patrón (predictibilidad) |
| `stack_arrange` | 65 | costumbre, calma |
| `count_chips` | 60 | cálculo |

El **gesto es la unidad**, pero el juego puede componer varios (un `smirk` con `lean_back` y `eye_roll` es "condescendencia"). La composición es una lista corta de gestos activos por personaje y turno.

## 6. Control de la expresión: la fuga

El personaje **no elige** sus gestos: los intenta controlar y **se le fugan** según su estado. La fuga de un canal depende de la emoción que la empuja, de la compostura efectiva y de la dificultad de control del gesto.

```text
   emoción (Mind)  →  exceso = max(0, afecto − umbral)          (0..255)
   compostura_ef   = composure_base + tell_control − fatiga − tilt   (0..100)
   fuga(canal)     = exceso × (100 − compostura_ef) × (100 − control) / 1 000 000
```

`fuga` satura a `[0,100]` y se compara con un umbral por canal: por debajo no se ve nada; por encima, el gesto aparece con esa intensidad. Los canales **autonómicos** (`pupil_dilate`, `hand_tremor`, `nose_flare`, `swallow`, `blink_fast`) tienen control bajo y por eso **siempre** delatan la activación, se quiera o no; los canales **volitivos** (`smile`, `lean_back`, `eye_roll`) se controlan mejor.

**Microexpresiones.** Con emoción intensa y compostura alta, el gesto no desaparece: dura muy poco (menos de ~200 ms) y es muy difícil de ver. Se modela con una ventana de tiempo y una detectabilidad alta solo si el observador estaba **atento** en ese instante (usa `attention_score`). Es el mecanismo del "se le escapó una sonrisa" que solo cazó el que miraba.

**Pesos configurables.** Todo el cálculo es paramétrico (`ExpressionParams`): umbrales por emoción, compostura por defecto, control por gesto, duración de microexpresión y detectabilidad. Ajustar los pesos cambia la "cantidad de póker" sin tocar el algoritmo.

## 7. Lectura de tells: el modelo del observador

Un personaje **solo revela** si otro **mira**. La lectura reutiliza la percepción y la atención de `eng::sim` y añade un modelo (`ReadModel`) por par (observador, objetivo).

```text
   emisor                         observador
   ┌────────────┐  gesto visible  ┌──────────────────────────────────────┐
   │ PsycheState│ ─────────────►  │ Senses (visión, cono, atención)       │
   │ fuga(gesto)│                 │  → TellObservation{gesto, fuerza}     │
   └────────────┘                 │  → ReadModel[objetivo]:               │
                                  │      veces gesto con mano fuerte      │
                                  │      veces gesto con mano débil       │
                                  │      → asociación gesto↔fuerza       │
                                  └──────────────────────────────────────┘
```

El `ReadModel` acumula, por gesto observado en un rival, cuántas veces ese gesto coincidió con una **mano fuerte** y cuántas con una **débil**. El problema del aprendizaje es que la fuerza solo se conoce cuando hay **showdown**: sin showdown no hay etiqueta y no se aprende. De ahí que "haya que conocerle bien" para fiarse de su tell.

```cpp
struct TellStat { u16 strong; u16 weak; };                 // por (objetivo, gesto)
struct ReadModel {
    TellStat tells[kMaxSeats][gesture_count];              // asociaciones aprendidas
    u16 hands_seen[kMaxSeats];                             // exposición total
    u8  archetype_guess[kMaxSeats];                        // estimación del arquetipo
    u8  uncertainty[kMaxSeats];                            // cuánto desconoce
};
```

**Inferencia (Bayes-lite entera).** Para un gesto `g` de un rival `t`, el observador estima la probabilidad de mano fuerte:

```text
   p_strong = (strong[g] + prior_strong) / (strong[g] + weak[g] + 1)
   p_weak   = 1 − p_strong
   indicio  = p_strong − p_weak                              ( −1 .. +1 )
```

y lo combina con la **línea de apuesta** (los tells de `Timing` son parte del mismo modelo) y con la **suspicacia** del observador: si `suspicion` es alta, descuenta los indicios "demasiado claros" (probables faroles invertidos). El resultado modula la decisión del observador: sube la agresión contra un tell de debilidad creíble, la baja contra uno de fuerza.

**El caso pardillo vs listillo.** Un pardillo tiene `deceit` bajo y `composure` baja: sus fugas son **genuinas** y la asociación `gesto↔fuerza` es fuerte y estable; fiarse paga. Un listillo tiene `deceit` alto y `composure` alta: puede **invertir** el tell (fingir debilidad con mano fuerte) o suprimirlo; su asociación es débil o negativa. El observador distingue ambos casos solo con **historial** (`hands_seen`, `strong`, `weak`): al principio no puede, y esa incertidumbre es la que da juego. La estimación del **arquetipo** (`archetype_guess`) actúa de *prior*: si cree que es un listillo, interpreta el tell al revés.

## 8. Evolución psicológica durante la partida

El estado cambia con los eventos de la mesa. Los eventos se registran como **recuerdos** (extendiendo `MemoryKind` o con un `TableMemoryKind` paralelo) y alimentan a la vez el `Mind` (afecto) y el `PsycheState` (compostura, confianza, tilt). Todo paramétrico (`PsycheParams`).

| Evento | Efecto afectivo (`Mind`) | Efecto de estado |
|---|---|---|
| `WonShowdown` | alegría↑ | confianza↑, racha↑ |
| `LostShowdown` | tristeza↑ | confianza↓, tilt↑ |
| `WonByFold` | alegría↑ | confianza↑ (pequeña) |
| `Bluffed` (farol exitoso) | alegría↑ | confianza↑, deceit_uso↑ |
| `CaughtBluffing` | vergüenza/ira↑ | confianza↓, tilt↑, suspicacia ajena↑ |
| `BadBeat` (perder con mano fuerte) | ira↑, tristeza↑ | **tilt↑↑**, paciencia↓ |
| `SuckedOut` (ganar con suerte) | alegría↑ | confianza↑ (puede volverse overconfidence) |
| `Slowrolled` | ira↑, odio↑ | rencor al rival↑, venganza |
| `WasRead` (le leyeron) | miedo↑ | composure_objetivo↑, cambia de tell |
| `ReadOpponent` (leyó a otro) | alegría↑, confianza↑ | atención↑ (refuerzo) |
| `LostBigPot` | tristeza↑, ira↑ | tilt↑, bankroll↓ |
| `WonBigPot` | alegría↑ | overconfidence, risk↑ |

La dinámica es una **evolución** de la personalidad *efectiva*: el carácter (rasgos base) no cambia, pero su **manifestación** sí, porque el estado modula compostura, agresión, farol y paciencia. Un **flemático** aguanta la racha; un **irascible** entra en tilt a la segunda derrota. La evolución también es **social**: los rivales actualizan su `ReadModel` y su `archetype_guess`, y el personaje puede detectar esa imagen (`table_image`) y cambiar de plan (metajuego).

```text
   mano 1        mano 2        mano 3 (bad beat)      mano 4           mano 5
   ────────►     ────────►     ───────────────►       ────────►        ────────►
   conf 50       conf 60       tilt 70, comp 20        comp 30          tilt 40
   comp 70       comp 65       (se le nota todo)       intenta disimular (le tiembla)
```

## 9. Señales secretas y convenciones (base del Mus)

El Mus necesita que dos o más jugadores **acuerden** un código de gestos para comunicarse sus jugadas sin que los demás lo entiendan. Eso es una **convención** (`Convention`): un mapa `gesto → significado` pactado, más un nivel de **disimulo** con que se ejecuta.

```cpp
struct Convention {
    u16 convention_id;                       // quién la comparte
    u8  meaning[gesture_count];              // gesto → señal (0 = sin significado)
    u8  concealment;                         // 0..100, cuánto la disimulan
    u8  exposure;                            // riesgo acumulado de ser descubierta
};
```

- **Emisión**: un gesto pactado se emite como cualquier otro, pero el observador que **no comparte** la convención no puede decodificarlo (solo ve un gesto sin sentido aparente).
- **Decodificación**: quien comparte la convención lo traduce a una señal (`SignalKind::ConventionBid`), que entra por el mismo camino que las demás señales (`receive_signals`, `apply_signal_effect`).
- **Riesgo de descubrimiento**: si un observador ve **repetición correlacionada** de gestos (mismo gesto en momentos de decisión, entre los mismos jugadores), puede `infer_convention` y subir su `suspicion`. El `exposure` sube con cada uso y baja con el disimulo; a partir de un umbral, la convención deja de ser secreta.
- **Pacto**: la convención se acuerda antes de jugar (o se descubre por observación); los NPC pueden negociarla entre sí y con el humano. En el Mus, además, hay señales que **deben** ser vistas por el compañero y **no** por los rivales: eso convierte el disimulo en parte central del juego.

Este apartado define el **sustrato** del Mus (convenciones, disimulo, descubrimiento); las reglas concretas del Mus (señas, envites, faroles) son otro documento cuando se implemente.

## 10. Integración con `eng::cards`

`eng::cards` es el primer consumidor. Cada asiento tiene una `Persona` y un `PsycheState`; los bots ya existentes (`ai/bot.hpp`, `decide_with_plan`) se **modulan** con la persona en lugar de sustituirse:

- **Decisión**: los parámetros del bot (`BotParams`: vpip, agresividad, farol, colchón) se derivan del arquetipo y se ajustan por estado (tilt sube agresión, miedo la baja). La aptitud (`odds_math`, `bluffing`, `opponent_reading`) escala el uso del equity y del rango.
- **Expresión**: al resolver una acción, se calcula la fuga de los gestos y se emiten como `TellObservation` a quienes miran.
- **Lectura**: el `OpponentModel` actual (frecuencias fold/call/raise) se **extiende** con el `ReadModel` de tells; el rango dinámico (`opponent_range_from_model`) pasa a combinar la línea de apuesta con los tells leídos.
- **Evolución**: al cerrar cada mano, `psyche.observe_result(...)` actualiza estado y memoria; los rivales actualizan su lectura.

```text
   ┌──────────────┐  decide  ┌──────────────┐  emite gestos  ┌──────────────┐
   │ Persona      │ ───────► │ eng::cards   │ ─────────────► │ ReadModel de │
   │ PsycheState  │ ◄─────── │ Table        │ ◄───────────── │ los rivales  │
   └──────────────┘  eventos └──────────────┘   tell leído    └──────────────┘
```

### 10.1 El humano también es un personaje

La capa de persona es **bidireccional**: el humano no es un observador externo, es un
personaje más con sus gestos y su estado, y **los NPC lo leen** igual que él los lee a
ellos. El motor no distingue el origen de una acción (IA o humano): ambos producen los
mismos eventos y las mismas fugas.

- **Gestos del humano**: el juego traduce la **entrada** a los canales de expresión. La
  interfaz puede capturar gestos de forma **explícita** (botones o combinaciones pactadas,
  como en el Mus) o **implícita** (el *timing*: cuánto tarda en responder, cuánto tarda en
  igualar, si apuesta de golpe). El canal `Timing` es el más natural para el humano: el
  **tiempo de respuesta** es una fuga objetiva que no depende de dibujar una cara.
- **Estado del humano**: el motor puede inferir tensión o prisa a partir del ritmo de
  entrada (respuestas rápidas y erráticas, pausas largas) y reflejarla en el estado del
  avatar humano, que los NPC leen.
- **Simetría de lectura**: `ReadModel` se construye sobre **cualquier** asiento, humano o
  IA. Un NPC puede aprender los tells del humano (que siempre tarda con mano fuerte, que
  hace *instant call* con basura) exactamente con el mismo mecanismo de §7.
- **El humano transmite a propósito**: además de los gestos involuntarios, puede emitir
  señales voluntarias (`SignalKind`, §9 y `communication.hpp`) hacia los NPC —burlarse,
  amenazar, calmar, pactar una convención— con las mismas reglas de credibilidad y
  disimulo. En el Mus, el humano es un jugador más del pacto de señas.

```text
   entrada (joystick/ratón/teclado)          mesa de juego
   ┌───────────────────────────┐            ┌──────────────────────┐
   │ gesto explícito (botón)   │ ─────────► │ Expression del humano│
   │ timing implícito (pausas) │            │  → ReadModel de los  │
   └───────────────────────────┘            │    NPC (aprenden)    │
                                            └──────────────────────┘
```

### 10.2 La misma capa sirve a ajedrez y Go

La expresión no es exclusiva de las cartas: cualquier juego por turnos se beneficia de un
rival **legible y vivo**. En `games/100_chess` y `games/101_go` el NPC usa los mismos
canales para reaccionar al **ritmo del humano**:

- **Bostezo** cuando el humano tarda demasiado en responder (canal `FaceMouth`, gesto
  `yawn`; disparado por `time_pressure` del lado humano y por `patience`/`boredom` del
  NPC).
- **Resoplido** (`sigh`) tras una jugada lenta o repetitiva; **impaciencia** (`finger_tap`,
  `leg_bounce`) mientras piensa el humano.
- **Sorpresa o incomodidad** cuando el humano juega una jugada fuerte o inesperada
  (`brow_raise`, `lean_back`, `swallow`), leída por el NPC como señal.
- **Satisfacción contenida** (`smirk`, `lean_back`) tras una buena jugada propia; **ira
  contenida** (`fist_clench`, `jaw_drop`) tras perder material.

En ajedrez y Go no hay información oculta que leer, así que el valor está en la
**experiencia**: el rival parece un contrincante real, con carácter y humor, no un motor
silencioso. El humano también puede emitir gestos (rendirse con desdén, celebrar), y el
NPC los interpreta para modular su propio estado (sube su `confidence` si el humano se
desanima, o su `aggression` si le provocan). La maquinaria es la misma que la de §5–§8; el
juego solo elige qué gestos tienen sentido en su contexto (un `yawn` no significa lo mismo
en un póker que en un tablero, y el **catálogo de gestos relevantes por juego** es parte
de la configuración).

### 10.3 Introspección simulada

Un NPC de información perfecta no lee al rival para decidir: lee **sus propios hechos de
motor**. La introspección simulada (`sim/introspection.hpp`) convierte lo que el juego ya
sabe en un estado afectivo que la cara y el cuerpo muestran:

```text
   hechos del motor                    introspección simulada
   ┌────────────────────────┐          ┌───────────────────────────┐
   │ mejor / segunda jugada │          │ confidence  (margen claro)│
   │ evaluación anterior    │ ───────► │ doubt       (margen fino) │
   │ libro de aperturas     │          │ pressure    (poco tiempo, │
   │ tiempo restante        │          │              desventaja)  │
   │ nº de jugadas legales  │          │ surprise / alert          │
   └────────────────────────┘          │ satisfaction / knowledge  │
                                       └───────────────────────────┘
                                                   │
                                                   ▼
                                        afecto (`Mind`) + estado (`PsycheState`)
                                                   │
                                                   ▼
                                             gestos y postura
```

- **Hechos** (`DecisionFacts`): margen entre la mejor y la segunda jugada (`best_score` −
  `second_score`), evaluación previa (`prev_eval`, para la sorpresa por la jugada del
  rival), acierto de libro, tiempo y número de jugadas. El puente
  `board/persona.hpp` los arma desde la búsqueda (`facts_from_search`) y tras la jugada del
  rival (`facts_after_opponent`), de modo que `eng::board` no conoce `eng::sim`.
- **Salidas** (`Introspection`): `confidence`, `doubt`, `pressure`, `surprise`, `satisfaction`,
  `alert` y `knowledge`. `introspection_apply` los vuelca en `Mind`/`PsycheState` (con
  `introspection_dominant` para elegir el nombre legible del estado).
- **Ritmo del rival**: `gestures_for_pace` (en `expression.hpp`) convierte la espera del
  humano (0 = responde ya, 255 = no responde) en impaciencia (`foot_tap`, `finger_tap`),
  resoplido (`sigh`) y, con esperas muy largas, bostezo (`yawn`) y desplome (`slump`). Un
  motor que espera **no es un motor mudo**: muestra que está esperando.

La introspección se calcula en el juego (que conoce búsqueda, libro y reloj) y alimenta la
misma maquinaria de expresión y lectura de §5–§8; sirve igual a ajedrez, Go o cualquier
juego con un motor evaluable. Verificación: HOST-206.


## 11. Presupuesto y footprint

Todo entero, sin heap y por bloques de capacidad fija, como el resto de `eng::sim`. Tamaños objetivo (a fijar por la sonda de codegen en la fase de implementación):

| Estructura | Bytes (m68k) | Notas |
|---|---|---|
| `Persona` (30 rasgos + aptitudes + defectos + arquetipo) | ~48 B | estática por personaje |
| `PsycheState` (estado + agregados) | ~24 B | cambia por turno |
| `Expression` (gestos activos del turno) | ~12 B | lista corta |
| `ReadModel` (por observador) | ~8 B × nº gestos + 8 B × asientos | crece con gestos y asientos |
| `Convention` | ~16 B | solo si el juego la usa |

Perfiles, coherentes con los de `eng::cards` (`N20`…`N512`): en el perfil mínimo se guarda un `Persona` reducida (arquetipo + compostura + deceit) y un `ReadModel` de un rival; en los altos, persona completa, lectura de todos los asientos y más historial. El número de gestos y de rivales leídos es parámetro de plantilla, igual que `MaxTrackers`/`MaxRelations`.

## 12. Determinismo y verificación

Todo el sistema es determinista: con la misma semilla y la misma secuencia de eventos, la personalidad, las fugas, las lecturas y la evolución son idénticas, en host y en Amiga. Eso permite **test host** reproducibles y **escenarios** (como `docs/debugging/investigaciones/npc-table-scenarios.md`): un pardillo al que se le escapa el póker, un listillo que finge, un observador que aprende tras N showdowns, un irascible que entra en tilt.

La verificación sigue las reglas de `docs/testing/README.md`: test host por pieza (`tests/host/NNN`), cruce `m68k` con la sonda de codegen (sin libcalls ni 68020) y, cuando exista, demo/juego como consumidor real. El plan por fases y la distribución de tests están en [ROADMAP_NPC_PSYCHOLOGY.md](../../guides/roadmap/ROADMAP_NPC_PSYCHOLOGY.md).

## 13. Fuera de alcance (por ahora)

- **Perfiles físicos** (fuerza, resistencia, destreza): pertenecen a la genética y al cuerpo de `eng::sim` (`genetics.hpp`, `body.hpp`); se abordarán para otros juegos, no ahora.
- **Voz sintetizada**: el canal `Voice` se modela como gesto (suspiro, carraspeo) sin audio real; si hay audio, se conectará a `eng::audio` como un evento más.
- **Reglas del Mus**: este documento define el sustrato (convenciones, disimulo, descubrimiento); el juego del Mus tendrá su propio diseño.

## 14. Inventario

| Área | Estado |
|---|---|
| `sim/psyche_traits.hpp` (20 rasgos de psique, 15 aptitudes, 16 defectos) | **Implementado**: HOST-199 |
| `sim/archetypes.hpp` (catálogo de ~39 arquetipos) | **Implementado**: HOST-199 |
| `sim/persona.hpp` (`Persona`, `materialize` con jitter) | **Implementado**: HOST-199 |
| `sim/expression.hpp` (canales, ~72 gestos, control, compostura y fuga) | **Implementado**: HOST-200 |
| `sim/read.hpp` (lectura de tells: Bayes-lite, prior, suspicacia) | **Implementado**: HOST-201 |
| `sim/psyche.hpp` (estado temporal y evolución de partida) | **Implementado**: HOST-202 |
| `sim/convention.hpp` (convenciones secretas, base del Mus) | **Implementado**: HOST-203 |
| `eng/cards/ai/persona_bot.hpp` (integración con `eng::cards`) | **Implementado**: HOST-204 |
| `sim/body.hpp` `pose_from_gesture` (postura desde gesto) | **Implementado** (cubierto por HOST-204/200) |
| `expression_from_input` (humano como personaje) | **Implementado**: HOST-205 |
| `sim/introspection.hpp` + `board/persona.hpp` (introspección simulada) | **Implementado**: HOST-206 |
| Avatares del juego | **Implementado en `games/200_holdem`**: caras y postura que reflejan los tells (build → run → analyze OK) |
| NPC reactivo al ritmo del humano en ajedrez/Go (P6.3) | **Implementado en `games/100_chess` y `games/101_go`**: cara que bosteza/se impacienta según la espera (`gestures_for_pace`) y expresiones desde la búsqueda (`introspect`) |
| Señales voluntarias del humano (P6.4) | **Parcial**: `expression_from_input` (HOST-205); falta que el NPC module su estado con las señales del humano |
| Escenarios de mesa (`docs/debugging/`) | **Implementado**: `docs/debugging/investigaciones/npc-table-scenarios.md` |

> Estado: P0 (persona/arquetipos), P1 (expresión/fuga), P2 (lectura), P3 (evolución), P4
> (convenciones), P5 (integración con `eng::cards`, rango con tells y avatares del juego),
> P6.1/P6.2 (humano como personaje) y P6.3 (NPC reactivo al ritmo del humano en ajedrez/Go
> con introspección simulada) implementados y verificados por test host (HOST-199…206) y,
> los juegos, por compilación m68k y build → run → analyze. El codegen 68000 está libre de
> libcalls e instrucciones 68020 y los tamaños m68k están fijados. Queda pendiente P6.4
> (que el NPC module su estado con las señales voluntarias del humano). El plan por
> fases y los criterios de cierre están en
> [ROADMAP_NPC_PSYCHOLOGY.md](../../guides/roadmap/ROADMAP_NPC_PSYCHOLOGY.md), fuente única
> del avance. Este documento describe el diseño vigente y no se duplica allí.