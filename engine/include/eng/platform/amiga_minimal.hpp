#pragma once

/// \file amiga_minimal.hpp
/// Backend Amiga minimo usado por las primeras demos.
///
/// Esta unidad es una pieza didactica: muestra como un backend concreto satisface
/// las necesidades del engine sin contaminar la logica de juego con detalles Amiga.
///
/// Politica actual:
/// - OS-friendly: usa Exec/AllocMem para reservar bloques.
/// - Close-to-the-metal: escribe registros custom cuando hace falta.
/// - Futuro: el backend podra tener modos configurables para usar ROM kernel,
///   takeover completo o una mezcla de ambos.

#include <eng/audio/audio_system.hpp>
#include <eng/core/domains.hpp>
#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/field/raster.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/sprite_collision.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga/blob.hpp>

namespace eng::amiga {

/// Perfil fisico/logico de la maquina objetivo.
///
/// Este perfil describe nuestras expectativas de diseno, no una deteccion dinamica
/// completa. `A500_1MB_Slow` significa 512 KB Chip + 512 KB trapdoor/bogo.
struct HardwareProfile {
	const char* id; ///< nombre del perfil (etiqueta legible)
	u16 chip_kb;    ///< Chip RAM en KB (la que ve el chipset por DMA)
	u16 slow_kb;    ///< Slow RAM en KB (trapdoor/bogo)
	u16 fast_kb;    ///< Fast RAM en KB (solo CPU)
	bool pal;       ///< `true` = timing PAL (50 Hz), `false` = NTSC
};

/// Perfil inicial realista para la maquina del proyecto.
constexpr HardwareProfile a500_1mb_slow {
	"A500_1MB_Slow",
	512,
	512,
	0,
	true,
};

/// Envoltorio del overlay de debug de WinUAE-DBG.
///
/// No dibuja en bitplanes Amiga. Es una herramienta de pruebas en host que permite
/// validar las primeras demos antes de escribir drivers graficos reales.
struct DebugOverlay {
	void clear();
	void text(s16 x, s16 y, const char* value, u32 rgb);
	void rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb);
	void filled_rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb);
};

/// Triangulo plano (coordenadas de pantalla) para el relleno por Blitter.
struct FlatTriangle {
	s16 x0 = 0; ///< X del vértice 0 (pantalla, píxeles)
	s16 y0 = 0; ///< Y del vértice 0
	s16 x1 = 0; ///< X del vértice 1
	s16 y1 = 0; ///< Y del vértice 1
	s16 x2 = 0; ///< X del vértice 2
	s16 y2 = 0; ///< Y del vértice 2
	u8 color = 0; ///< índice de color (paleta)
};

/// Backend Amiga minimo.
///
/// Sus responsabilidades actuales son:
/// - reservar bloques base para las arenas del engine;
/// - esperar VBlank;
/// - escribir colores custom simples;
/// - exponer el overlay de debug;
/// - dejar claro donde usamos ROM kernel y donde tocamos hardware directo.
class MinimalBackend {
public:
	using Profile = HardwareProfile;

	constexpr explicit MinimalBackend(Profile profile = a500_1mb_slow)
		: m_profile(profile) {}
	~MinimalBackend();

	MinimalBackend(const MinimalBackend&) = delete;
	MinimalBackend& operator=(const MinimalBackend&) = delete;

	/// Inicializacion minima del backend.
	void boot();

	/// Reserva bloques base y los entrega a `MemorySystem`.
	///
	/// Importante: esto no "posee toda la RAM del Amiga". Solo pide al ROM kernel los
	/// bloques solicitados. En modo takeover futuro, esta misma API podra poblarse
	/// con rangos fisicos conocidos sin pasar por Exec.
	bool configure_memory(const MemoryConfig& config);

	/// Libera los bloques reservados con Exec.
	void release_memory();

	/// Espera al comienzo de VBlank leyendo VPOSR directamente.
	///
	/// Firma de un servicio del backend: recibe el contexto por **referencia** y la
	/// línea de raster. Sustituye al par `void (*)(void*, u16) + void*`.
	template <class C>
	using Service = void (*)(C& ctx, u16 vpos);

	/// Almacenamiento de un servicio tipado: el thunk (instanciado por `C`) recupera
	/// la rutina y el contexto. El llamador conserva vivo su contexto.
	struct ServiceSlot {
		void (*thunk)(void* slot, u16 vpos) = nullptr; ///< trampolín que reconstruye e invoca el servicio
		alignas(void*) eng::u8 fn[sizeof(void*)] {};   ///< bytes del funtor (tamaño fijo, ABI de IRQ)
		void* ctx = nullptr;                           ///< contexto del llamador (vivo durante el servicio)
	};

	/// Trampolín C de un servicio: **reconstruye** el `Service<C>` desde los bytes del slot
	/// (sin `std::function`) y lo invoca con `(ctx, vpos)`. Es el `thunk` que fija `fill_slot`.
	template <class C>
	static void service_thunk(void* slot_bytes, u16 vpos) {
		auto* s = static_cast<ServiceSlot*>(slot_bytes);
		Service<C> fn {};
		for (eng::u32 i = 0; i < sizeof(fn); ++i) reinterpret_cast<eng::u8*>(&fn)[i] = s->fn[i];
		fn(*static_cast<C*>(s->ctx), vpos);
	}

	/// Registra un servicio en un `ServiceSlot`: guarda el funtor como **bytes** (tamaño fijo,
	/// apto para el ABI de interrupción) y su contexto. Empareja con `service_thunk<C>`.
	template <class C>
	static void fill_slot(ServiceSlot& slot, Service<C> fn, C& ctx) {
		slot.thunk = &MinimalBackend::service_thunk<C>;
		for (eng::u32 i = 0; i < sizeof(fn); ++i) {
			reinterpret_cast<eng::u8*>(&slot.fn)[i] = reinterpret_cast<const eng::u8*>(&fn)[i];
		}
		slot.ctx = &ctx;
	}

	/// Token para `wait_vblank` (la rutina no se almacena: se pasa al bucle directo).
	template <class C>
	struct DirectToken {
		eng::Ref<C> ctx {};      ///< contexto del llamador (no propietario, anulable)
		Service<C> fn = nullptr; ///< rutina de espera (viaja dentro del token)
	};

	/// Trampolín de `wait_vblank` para el token directo: el funtor viaja EN el token (no en un
	/// slot persistente), así que sólo lo invoca con `(ctx, vpos)`. El contexto se guarda
	/// como `eng::Ref<C>` (observador **no propietario**, anulable) en vez de `C*`.
	template <class C>
	static void direct_thunk(void* token_bytes, u16 vpos) {
		auto* t = static_cast<DirectToken<C>*>(token_bytes);
		t->fn(*t->ctx.get(), vpos);
	}

	/// `task`/`user` son un **hook de tarea ociosa** opcional: se ejecuta repetidamente
	/// mientras la CPU espera (no hay trabajo de juego que hacer). `task` recibe la
	/// **linea de raster actual** (`vpos`) para que adapte su carga. Sirve para avanzar
	/// trabajo de fondo que no debe competir por el bus con el CPU del frame (p. ej.
	/// las tareas de `eng::task::BackgroundQueue` y las fases del C2P). Ver
	/// `Engine::run_frames`.
	template <class C>
	void wait_vblank(Service<C> task, C& ctx) {
		DirectToken<C> token { eng::Ref<C> {ctx}, task };
		wait_vblank_run(&MinimalBackend::direct_thunk<C>, &token);
	}
	void wait_vblank() { wait_vblank_run(nullptr, nullptr); }

	/// Linea de raster actual (VPOSR). Barata; el tick del juego la usa para medir su
	/// presupuesto (cuanto raster consume cada frame).
	u16 current_raster_line() const;

	/// Lee el contador **TOD** (tiempo del dia) de la CIA-A: 24 bits a 50/60 Hz, con el
	/// orden de latch `TODHI -> TODMID -> TODLO` (leer TODHI lo congela). Ver
	/// `eng/core/rtc.hpp` y `amiga-bootcamp/01_hardware/common/cia_chips.md`.
	u32 cia_tod_ticks() const;

	/// Tarea opcional que el backend ejecuta mientras **espera al Blitter** (`BBUSY`).
	/// El engine la usa para drenar las tareas de fondo (`eng::task::BackgroundQueue`)
	/// en vez de girar en vacio. Recibe la linea de raster (`vpos`). `nullptr` la apaga.
	template <class C>
	void set_blitter_service(Service<C> task, C& user) {
		fill_slot(m_blitter_slot, task, user);
		install_blitter_service(m_blitter_slot);
	}

	/// Instala la IRQ de **VBlank** (nivel 3) y hace que el backend ejecute
	/// `task(user, vpos)` en cada VBlank. Es el **latido del juego**: `Engine` la usa en
	/// modo interrupt-driven para correr `update`/`render` con deadline de un frame,
	/// dejando el bucle principal al trabajo de fondo cooperativo (que la IRQ preempta).
	/// `task` debe ser corta. Devuelve false si ya habia una instalada.
	template <class C>
	bool set_vblank_service(Service<C> task, C& user) {
		if (task == nullptr) return false;
		fill_slot(m_vblank_slot, task, user);
		return install_vblank_service(m_vblank_slot);
	}

	/// Desinstala la IRQ de VBlank (restaura el vector de nivel 3 y `INTENA`).
	void clear_vblank_service();

	/// Servicio de **blit** por IRQ (nivel 3, `BLIT`): `task(user, vpos)` se ejecuta cada
	/// vez que el Blitter termina. Comparte el autovector de nivel 3 con el VBlank, asi
	/// que el backend usa un **unico** handler que despacha por `INTREQR`. Util para
	/// encadenar blits (el handler programa el siguiente) sin *polling* de `BBUSY`.
	template <class C>
	bool set_blit_service(Service<C> task, C& user) {
		if (task == nullptr) return false;
		fill_slot(m_blit_slot, task, user);
		return install_blit_service(m_blit_slot);
	}

	/// Desinstala el servicio de blit.
	void clear_blit_service();

	/// Activa/desactiva la prioridad del Blitter sobre la CPU (`DMACON` bit 10, BLTPRI,
	/// el "blitter nasty" de OCS). Con `true`, el Blitter NO cede sus slots a la CPU: el
	/// DMA avanza a plena velocidad aunque la CPU tenga trabajo (esta se detiene). Util
	/// para cadenas de blits que deben terminar cuanto antes (p. ej. el C2P encadenado).
	void set_blitter_priority(bool enabled);

	/// Arranca un motor de fondo por **timer A de la CIA-A** (IRQ nivel 2). El timer
	/// corre **continuo** a `latch / 709379` s por tic y llama a `task(user, vpos)` en
	/// cada uno (una rebanada corta), de forma independiente al frame. Tambien sirve
	/// como base de un **reloj de tiempo real** (la CIA tiene TOD por hardware). Ver
	/// `BACKGROUND_TASKS.md`.
	template <class C>
	bool background_timer_start(u16 latch, Service<C> task, C& user) {
		if (task == nullptr) return false;
		fill_slot(m_timer_slot, task, user);
		return install_timer_service(latch, m_timer_slot);
	}

	/// Detiene el motor por timer (para el timer, enmascara la CIA y restaura el vector).
	void background_timer_stop();

	/// Escribe un registro COLORxx. `rgb444` usa el formato nativo OCS.
	void set_color(u8 index, u16 rgb444);

	/// Toma el control completo del display y arranca la primera copperlist.
	///
	/// Debe llamarse UNA sola vez, al iniciar la demo (tipicamente desde
	/// `init()`/la primera composicion). Congela el sistema que AmigaDOS dejo
	/// vivo (interrupciones y DMA de exec/graphics/intuition, sprite del
	/// puntero y disco), programa `copper_words` en COP1LC, espera el arranque
	/// de VBlank para que el Copper no parta a media pantalla, y arranca
	/// master+copper con un COPJMP1 alineado al inicio de linea. A partir de
	/// aqui el engine no vuelve a usar exec: el bucle es espera activa por
	/// VPOSR. Es close-to-the-metal: el sistema operativo no arbitra esta lista.
	void takeover_display(const u16* copper_words);

	/// Cambia la copperlist activa por otra ya construida en Chip RAM (swap).
	///
	/// NO toma el control del display ni dispara COPJMP1: solo actualiza
	/// COP1LC. El Copper recarga el puntero solo al comienzo del proximo
	/// VBlank, asi que es seguro llamarla en cualquier punto del frame (doble
	/// buffer: la 201 la llama cada frame). Requiere que `takeover_display`
	/// se haya llamado al menos una vez; en caso contrario se comporta como
	/// una toma de control de un solo intento (compatibilidad con drivers
	/// antiguos que solo conocian `install_copper_list`).
	void install_copper_list(const u16* copper_words);

	/// Ejecuta los trabajos hardware descritos por un `FramePlan`.
	///
	/// Por ahora solo materializa BOBs enmascarados mediante Blitter. Los parches de
	/// paleta pertenecen a la escena (offsets internos de su copperlist).
	bool execute_frame_plan(const graphics::FramePlan& plan);

	/// **Capacidades de rasterizado** del backend: OCS/AGA tienen Blitter (bus de 16 bits;
	/// AGA admite FMODE 32/64) con fill/line/shift/minterms. Un backend host declararía
	/// `blitter = false`. Ver `field::RasterCaps`.
	[[nodiscard]] constexpr eng::field::RasterCaps raster_caps() const {
#if defined(K_AGA)
		// Target AGA (A1200/A4000/CD32): bus de 64 bits con FMODE=4x.
		return eng::field::RasterCaps { true, 64u, true, true, true, true, 60u };
#else
		// Target OCS (A500): bus de 16 bits.
		return eng::field::RasterCaps { true, 16u, true, true, true, true, 60u };
#endif
	}

	/// Instala en la escena el **rasterizador** coherente con `raster_caps()` (Blitter si
	/// lo hay, CPU si no) y una `RasterPolicy` por defecto (`Auto` + umbral). La escena no
	/// conoce al backend: este solo le pasa la elección.
	template <class Scene>
	void install_raster(Scene& scene) const {
		const eng::field::RasterCaps caps = raster_caps();
		const eng::field::AccelMode mode =
			caps.blitter ? eng::field::AccelMode::Auto : eng::field::AccelMode::Cpu;
		const eng::u16 min_px = caps.blitter ? static_cast<eng::u16>(64u) : static_cast<eng::u16>(0u);
		scene.set_raster(caps.blitter ? &eng::field::kBlitterRaster : &eng::field::kCpuRaster,
				 eng::field::RasterPolicy {mode, min_px, true});
	}

	/// Base de registros custom (`$dff000`). Para rutinas de lote `inline` (p. ej.
	/// `eng::amiga::OrBlobBatch`) que programan hardware sin un `jsr` por objeto.
	volatile u16* custom_registers() const {
		return reinterpret_cast<volatile u16*>(0xdff000);
	}

	/// Rellena triangulos planos con el **Blitter**: por cada triangulo dibuja el
	/// contorno (line mode XOR, ONEDOT) + area fill inclusivo en un plano-mascara
	/// de 1 bit y luego hace cookie-cut de la mascara a cada bitplane segun el
	/// color. `mask` es un plano 1 bit en Chip RAM con el mismo `row_bytes` y
	/// tamano que el destino. Es la ruta rapida de hardware; el llamador conserva
	/// las rutinas CPU por tramos de byte como alternativa (p. ej. `fill_span`).
	bool fill_triangles_blitter(const FlatTriangle* tris, u32 count,
				    eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes,
				    eng::MaskBuffer mask);

	/// Ruta robusta de relleno con Blitter: el llamador construye con la CPU una
	/// **máscara** de 1 bit (por tramos, `fill_span`) y el Blitter hace el
	/// **cookie-cut** de la máscara a cada bitplane de color (`D=A|D` si el bit del
	/// color esta a 1, `D=~A&D` si a 0). Combina lo mejor de ambos: la CPU escribe 1
	/// plano y el Blitter (en paralelo) materializa los planos de color. La region
	/// se ajusta a palabra automaticamente.
	bool blit_fill_from_mask(eng::MaskBytes mask, eng::PlaneBytes dst, u8 planes, u16 row_bytes,
				 u32 plane_bytes, s16 x, s16 y, u16 w, u16 h, u8 color);

	/// Dibuja una linea por hardware con el Blitter, en la secuencia EXACTA de
	/// `DrawObject` de la demo `wireframe` del demoscene (line mode OR, sin ONEDOT):
	/// `bltcon0 = rorw(x0&15,4)|BC0F_LINE_OR`, `bltcon1 = LINEMODE|SUD/AUL/SUL|
	/// SIGNFLAG|rorw(x0&15,4)`, `bltamod = derr-dmax`, `bltbmod = dmin*2`,
	/// `bltapt = (void*)derr`, `bltsize = (dmax<<6)+66`. `row_bytes` = bytes por
	/// fila del plano (el original usa WIDTH/8). Linea OR sobre el destino.
	bool blitter_line(eng::PlaneBytes plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1);

	/// **Colisión pixel-perfect por Blitter**: hace `scratch = a & b` (minterm `$C0`) por
	/// plano y devuelve `true` si alguna palabra del rect es distinta de 0. `words`×`rows`
	/// es el rect (en palabras de 16 px × filas). Referencia CPU: `field::collide_cpu`.
	/// **Verificada en hardware**: self-test de la demo 077 (colisión y no-colisión).
	bool blitter_collide(eng::PlaneBytes a, eng::PlaneBytes b, eng::PlaneBytes scratch,
			     u8 planes, u16 row_bytes, u32 plane_bytes, u16 words, u16 rows);

	/// **Colisión de hardware de sprites**: configura `CLXCON` (`$DFF098`) según
	/// `graphics::sprite_collision.hpp` (qué pares de sprite y bitplanes participan).
	void set_sprite_collision(graphics::SpriteCollisionConfig cfg);

	/// Lee y decodifica `CLXDAT` (`$DFF00E`). **Se autolimpia al leer**: llamar una vez por
	/// frame. Devuelve qué colisiones se registraron desde la última lectura.
	[[nodiscard]] graphics::SpriteCollisionResult read_sprite_collision();

	/// **Relleno de polígonos compuesto por bitplane** (Blitter): para cada plano `p`,
	/// limpia el plano, dibuja el contorno XOR (ONEDOT) de las caras cuyo color tiene el
	/// bit `p` a 1 y hace **un area fill** (`FILL_XOR`); un fill por plano en vez de uno
	/// por polígono. `dest` es el bitmap contiguo (se escribe en su sitio). Referencia CPU:
	/// `graphics::fill_polygons_by_plane_cpu`. Verificada por el self-test de 077.
	bool fill_polygons_by_plane(const graphics::PlanePolygon* faces, u32 n_faces,
				    eng::PlaneBytes dest, u16 row_bytes, u32 plane_bytes,
				    u8 planes, u16 width, u16 height);

	/// Línea por Blitter en modo `ONEDOT` con minterm **EOR** (`BC0F_LINE_EOR`),
	/// secuencia EXACTA de `DrawObject` de `flatshade-convex`: se usa para el
	/// contorno de polígonos (un píxel por fila) que luego rellena
	/// `blitter_area_fill`. Sin `SIGNFLAG` (como el original). Variante autónoma:
	/// fija los comunes (`blitter_lines_eor_begin`), dibuja y espera al final.
	/// `d_base` replica el truco del original (`bltdpt = planes`, la base del
	/// bitmap, NO la dirección calculada): si es `nullptr` se usa la dirección de
	/// la línea.
	bool blitter_line_eor(eng::PlaneBytes plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1,
			      eng::u8* d_base = nullptr);

	/// Inicia los registros comunes del modo línea EOR (ONEDOT) para una
	/// secuencia de líneas. Escribe `BLTAFWM/ALWM`, `BLTADAT`, `BLTBDAT`,
	/// `BLTCMOD`, `BLTDMOD` una sola vez, como el preludio de `DrawObject` de
	/// `flatshade-convex`. NO espera al Blitter: el llamador sincroniza antes de la
	/// primera `blitter_line_eor_draw`.
	void blitter_lines_eor_begin(u16 row_bytes);

	/// Parámetros de línea EOR (ONEDOT) precalculados: son independientes del plano
	/// (solo cambia el puntero C entre planos de la misma arista). Permiten calcular
	/// el Bresenham UNA vez por arista y reutilizarlo en los N planos del color,
	/// como hace el `DrawObject` del original (que avanza `bltcpt += plane_bytes`).
	/// `row_offset` es el desplazamiento de la línea dentro del plano.
	/// Alias del tipo de dominio `eng::graphics::LineEor` (las demos usan el nombre de
	/// dominio; el backend rellena sus campos).
	using LineEorParams = eng::graphics::LineEor;

	/// Calcula los parámetros de una línea EOR sin programar el Blitter. Devuelve
	/// `false` para aristas horizontales (`y0 == y1`), que no aportan contorno.
	bool blitter_line_eor_prepare(LineEorParams& out, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1);

	/// Programa UNA línea EOR con parámetros ya preparados (`blitter_line_eor_prepare`),
	/// asumiendo los comunes fijados por `blitter_lines_eor_begin`. `plane_ptr` es el
	/// inicio del plano; `d_base` replica el truco del original (`bltdpt = planes`).
	void blitter_line_eor_draw(const LineEorParams& p, eng::u8* plane_ptr, eng::u8* d_base);

	/// Relleno por **area fill XOR** (paint-bucket del Blitter), port de
	/// `BitmapFillFast` de `flatshade-convex`: parte de la última palabra del bitmap
	/// (`planes` planos contiguos de `plane_bytes`) y rellena el hueco dejado por el
	/// contorno (`blitter_line_eor`). `width` en píxeles (múltiplo de 16).
	/// `wait=false` lanza el fill sin esperarlo (para solaparlo con trabajo de CPU o
	/// con la espera de VBlank); el llamador debe sincronizar con `wait_blitter()`
	/// antes de reprogramar registros o mostrar el buffer.
	bool blitter_area_fill(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 width, u16 height,
			       bool wait = true);

	/// Rellena un **poligono convexo** (puntos de pantalla `xs`/`ys`, `n` vertices) con
	/// `color` en `planes` planos. Limpia la mascara de su bbox, traza el contorno
	/// (`ONEDOT`), hace area fill inclusivo (`FILL_OR`) y cookie-cut a cada plano.
	/// Da caras solidas limpias (sin el filtrado del area-fill `XOR` en vertices).
	bool blitter_fill_polygon(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes,
				  const s16* xs, const s16* ys, u8 n, u8 color, eng::MaskBuffer mask);

	/// Igual que `blitter_fill_polygon` pero con la geometria del destino EXPLICITA,
	/// para servir tanto a planos CONTIGUOS (base + `plane_stride` entre planos,
	/// `row_stride` = `row_bytes`) como INTERLEAVED (una fila de cada plano seguida:
	/// `plane_stride` = `row_bytes`, `row_stride` = `planes*row_bytes`). `plane_base`
	/// es el inicio del plano 0 (fila 0) del bitmap; `mask` es un plano 1 bit con
	/// stride `row_bytes`. `bitmap_w`/`bitmap_h` acotan el recorte al bitmap real
	/// (el playfield puede ser mas bajo que 256). Es la ruta que usa el engine para
	/// el relleno por Blitter de un `CanvasPlayfield` interleaved (capa FG en DPF).
	bool blitter_fill_polygon_strided(eng::u8* plane_base, u8 planes, u32 plane_stride,
					  u32 row_stride, u16 row_bytes, u16 bitmap_w, u16 bitmap_h,
					  const s16* xs, const s16* ys, u8 n, u8 color, eng::MaskBuffer mask);

	/// Borra (D=0) una region de `w`x`h` en `planes` planos contiguos con separacion
	/// `plane_bytes`, alineando a palabra. Equivale a `BlitterClear` del demoscene.
	bool blitter_clear(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h,
			   bool wait = true);

	/// Limpia (D=0) el rectangulo `words` x `rows` que empieza en `(wx0, y0)` de UN
	/// plano (util para acotar el borrado a la bounding-box del objeto). `wx0`
	/// alineado a palabra.
	bool blitter_clear_rect(eng::PlaneBytes plane, u16 row_bytes, u16 wx0, s16 y0, u16 words, u16 rows,
				bool wait = true);

	/// Una entrada del lote de BOBs OR intercalados (port de `DrawObject` de bobs3d).
	/// Alias del tipo de dominio `eng::graphics::OrBob`.
	using OrBobEntry = eng::graphics::OrBob;

	/// Dibuja un **lote de BOBs OR intercalados** fijando los campos CONSTANTES del
	/// blit UNA sola vez (`BLTCON1`, `BLTAFWM/ALWM`, `BLTAMOD`, `BLTBMOD`, `BLTDMOD`,
	/// `BLTSIZE`); por objeto solo escribe `BLTCON0` (minterm `A_OR_B` + ASH),
	/// `BLTAPT`, `BLT(B/D)PT` y espera. Es el bucle exacto de `DrawObject` de
	/// `demoscene-repo-orig/effects/bobs3d/bobs3d.c` (`A_OR_B`, `B = D = destino`,
	/// atlas **denso** sin guarda y 3 palabras por fila).
	///
	/// `words` = palabras por fila (`BOBW/16`), `height` = filas totales
	/// (`BOB alto * planos`), `source_modulo` = `BLTAMOD` (0 con atlas denso),
	/// `dest_modulo` = `BLTBMOD`=`BLTDMOD` (`bytes_por_fila_plano - words*2`).
	/// Las entradas se procesan en orden; el Blitter se espera antes de programar
	/// cada una (como el original). Es la ruta de coste cero para muchos BOBs.
	bool blitter_or_bobs(const OrBobEntry* entries, u32 count, u16 words, u16 height,
			     s16 source_modulo, s16 dest_modulo);

	/// Variante **en streaming** del lote de BOBs, sin array intermedio: el llamador
	/// recorre sus datos y por cada objeto calcula su entrada y la lanza. Es la
	/// estructura exacta de `DrawObject` (el calculo del vertice y la programacion del
	/// blit en el mismo bucle), y evita escribir/leer la lista de entradas.
	///
	///   blitter_or_bobs_begin(words, height, amod, dmod);
	///   for (cada objeto) blitter_or_bobs_one(src, dst, shift);
	///   blitter_or_bobs_end();
	void blitter_or_bobs_begin(u16 words, u16 height, s16 source_modulo, s16 dest_modulo);
	/// Lanza UN BOB (espera al anterior, escribe `BLTCON0` OR + `APT/BPT/DPT` + start).
	void blitter_or_bobs_one(const void* source, void* dest, u8 shift);
	/// Espera al ultimo BOB. `false` si el Blitter no responde.
	bool blitter_or_bobs_end();

	/// Area fill `XOR` del mismo rectangulo de UN plano (semilla = ultima palabra
	/// del rectangulo, recorrido descendente). Port de `BitmapFillFast` acotado a
	/// una caja, para no barrer el bitmap completo cada frame.
	bool blitter_area_fill_rect(eng::PlaneBytes plane, u16 row_bytes, u16 wx0, s16 y0, u16 words, u16 rows,
				    bool wait = true);

	/// Escribe el registro de datos de un bitplane (`BLTxDAT`, $110 + 2*plane). Lo
	/// usa fire-rgb para los bits HAM fijos de los planos 4/5 (`0x7777`/`0xcccc`).
	void set_bitplane_dat(u8 plane, u16 value);

	/// Estado del C2P 4 bpp por Blitter (portado de `ChunkyToPlanar` de fire-rgb).
	/// Alias del tipo de dominio `eng::graphics::C2p4` (las demos usan el nombre de
	/// dominio; el backend programa las fases).
	using C2p4State = eng::graphics::C2p4;

	/// Ejecuta UNA fase del C2P 4 bpp (0..12, como el original) y espera al Blitter.
	/// Devuelve false si el Blitter no responde. La fase 12 (parcheo de BPLxPT) la
	/// gestiona el llamador (aqui solo se avanza).
	bool c2p_4bpp_step(C2p4State& state);

	/// Programa la fase actual del C2P 4 bpp **sin esperar** al Blitter y avanza el
	/// contador de fase. Es la mitad "arranca y sigue" de `c2p_4bpp_step`, pensada
	/// para solapar el C2P con el trabajo de CPU del frame siguiente (lo que el
	/// original hacia por **interrupcion de blit**). El llamador debe comprobar
	/// `blitter_busy()` antes de programar la fase siguiente.
	bool c2p_4bpp_program(C2p4State& state);

	/// `true` mientras el Blitter esta ocupado (bit BBUSY de DMACONR). Permite
	/// encadenar las fases del C2P por sondeo sin bloquear la CPU.
	bool blitter_busy() const;

	/// Espera a que el Blitter termine. Util para solapar un blit con trabajo de
	/// CPU: se lanza el blit con `wait=false` (clear/area fill), se hace CPU, y se
	/// sincroniza aqui antes de reprogramar registros o mostrar el buffer.
	bool wait_blitter();

	/// Inicia el subsistema de audio (SFX mixer + reproductores de música).
	/// Debe llamarse después de `configure_memory` (necesita el bloque Chip para el
	/// buffer del mixer) y después de `takeover_display` (el mixer instala su
	/// interrupción de audio).
	bool audio_init() {
		return m_audio.init(m_memory);
	}

	/// Acceso al subsistema de audio (SFX + música). Úsalo desde el juego para
	/// `play_sfx`/`play_music` sin instanciar un `AudioSystem` por demo.
	eng::audio::AudioSystem& audio() { return m_audio; }
	const eng::audio::AudioSystem& audio() const { return m_audio; }

	/// Activa/desactiva warp mode del emulador mediante la ayuda de WinUAE-DBG.
	void set_warpmode(bool enabled);

	constexpr const Profile& profile() const { return m_profile; }
	constexpr MemorySystem& memory() { return m_memory; }
	constexpr const MemorySystem& memory() const { return m_memory; }
	constexpr const MemoryReport& memory_report() const { return m_memory_report; }
	/// Arranques reales de BLTSIZE durante la última ejecución del plan.
	constexpr u32 blitter_starts() const { return m_blitter_starts; }
	constexpr DebugOverlay& debug() { return m_debug; }

private:
	// Instaladores (no-plantilla): enlazan el slot con los globales del ISR y arman
	// los vectores de interrupción. El API público es tipado (referencias).
	void wait_vblank_run(void (*thunk)(void*, u16), void* user);
	void install_blitter_service(ServiceSlot& slot);
	bool install_vblank_service(ServiceSlot& slot);
	bool install_blit_service(ServiceSlot& slot);
	bool install_timer_service(u16 latch, ServiceSlot& slot);

	Profile m_profile; ///< perfil de máquina configurado
	MemorySystem m_memory {}; ///< arenas (Chip/Slow/Frame) entregadas al engine
	MemoryReport m_memory_report {}; ///< informe de la reserva de memoria
	DebugOverlay m_debug {}; ///< overlay de debug (host/WinUAE)
	eng::audio::AudioSystem m_audio {}; ///< sistema de audio
	ServiceSlot m_blitter_slot {}; ///< servicio de espera de Blitter
	ServiceSlot m_vblank_slot {}; ///< servicio de VBlank
	ServiceSlot m_blit_slot {}; ///< servicio de fin de blit (IRQ de blit)
	ServiceSlot m_timer_slot {}; ///< servicio del timer de CIA
	void* m_chip_alloc = nullptr; ///< bloque base de Chip RAM reservado
	u32 m_chip_alloc_size = 0; ///< tamaño (KB) del bloque de Chip RAM
	void* m_slow_alloc = nullptr; ///< bloque base de Slow RAM reservado
	u32 m_slow_alloc_size = 0; ///< tamaño (KB) del bloque de Slow RAM
	void* m_frame_alloc = nullptr; ///< bloque base de Frame scratch reservado
	u32 m_frame_alloc_size = 0; ///< tamaño (KB) del bloque de Frame scratch
	u32 m_blitter_starts = 0; ///< contador de blits lanzados (diagnóstico)
	/// Estado del lote de BOBs no-inline (`blitter_or_bobs_begin/one/end`): delega en
	/// la misma implementacion `inline` de `blob.hpp` que usa el camino de coste cero.
	eng::amiga::OrBlobBatch m_or_bob {};
	/// true una vez que la primera copperlist ha tomado el control completo del
	/// display (INTENA/INTREQ/DMACON apagados e interrupciones del sistema
	/// congeladas). Las instalaciones posteriores son solo swaps de puntero.
	bool m_display_taken = false;
};

} // namespace eng::amiga
