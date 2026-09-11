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
#include <eng/core/types.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::amiga {

/// Perfil fisico/logico de la maquina objetivo.
///
/// Este perfil describe nuestras expectativas de diseno, no una deteccion dinamica
/// completa. `A500_1MB_Slow` significa 512 KB Chip + 512 KB trapdoor/bogo.
struct HardwareProfile {
	const char* id;
	u16 chip_kb;
	u16 slow_kb;
	u16 fast_kb;
	bool pal;
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
	s16 x0 = 0;
	s16 y0 = 0;
	s16 x1 = 0;
	s16 y1 = 0;
	s16 x2 = 0;
	s16 y2 = 0;
	u8 color = 0;
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
	void wait_vblank();

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
	/// paleta pertenecen al driver grafico (`StaticEhbScene`) porque son offsets
	/// internos de su copperlist.
	bool execute_frame_plan(const graphics::FramePlan& plan);

	/// Rellena triangulos planos con el **Blitter**: por cada triangulo dibuja el
	/// contorno (line mode XOR, ONEDOT) + area fill inclusivo en un plano-mascara
	/// de 1 bit y luego hace cookie-cut de la mascara a cada bitplane segun el
	/// color. `mask` es un plano 1 bit en Chip RAM con el mismo `row_bytes` y
	/// tamano que el destino. Es la ruta rapida de hardware; el llamador conserva
	/// las rutinas CPU por tramos de byte como alternativa (p. ej. `fill_span`).
	bool fill_triangles_blitter(const FlatTriangle* tris, u32 count,
				    u8* dst, u8 planes, u16 row_bytes, u32 plane_bytes,
				    u8* mask);

	/// Ruta robusta de relleno con Blitter: el llamador construye con la CPU una
	/// **máscara** de 1 bit (por tramos, `fill_span`) y el Blitter hace el
	/// **cookie-cut** de la máscara a cada bitplane de color (`D=A|D` si el bit del
	/// color esta a 1, `D=~A&D` si a 0). Combina lo mejor de ambos: la CPU escribe 1
	/// plano y el Blitter (en paralelo) materializa los planos de color. La region
	/// se ajusta a palabra automaticamente.
	bool blit_fill_from_mask(const u8* mask, u8* dst, u8 planes, u16 row_bytes,
				 u32 plane_bytes, s16 x, s16 y, u16 w, u16 h, u8 color);

	/// Dibuja una linea por hardware con el Blitter, en la secuencia EXACTA de
	/// `DrawObject` de la demo `wireframe` del demoscene (line mode OR, sin ONEDOT):
	/// `bltcon0 = rorw(x0&15,4)|BC0F_LINE_OR`, `bltcon1 = LINEMODE|SUD/AUL/SUL|
	/// SIGNFLAG|rorw(x0&15,4)`, `bltamod = derr-dmax`, `bltbmod = dmin*2`,
	/// `bltapt = (void*)derr`, `bltsize = (dmax<<6)+66`. `row_bytes` = bytes por
	/// fila del plano (el original usa WIDTH/8). Linea OR sobre el destino.
	bool blitter_line(u8* plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1);

	/// Borra (D=0) una region de `w`x`h` en `planes` planos contiguos con separacion
	/// `plane_bytes`, alineando a palabra. Equivale a `BlitterClear` del demoscene.
	bool blitter_clear(u8* dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h);

	/// Inicializa el subsistema de audio (SFX mixer + reproductores de música).
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
	Profile m_profile;
	MemorySystem m_memory {};
	MemoryReport m_memory_report {};
	DebugOverlay m_debug {};
	eng::audio::AudioSystem m_audio {};
	void* m_chip_alloc = nullptr;
	u32 m_chip_alloc_size = 0;
	void* m_slow_alloc = nullptr;
	u32 m_slow_alloc_size = 0;
	void* m_frame_alloc = nullptr;
	u32 m_frame_alloc_size = 0;
	u32 m_blitter_starts = 0;
	/// true una vez que la primera copperlist ha tomado el control completo del
	/// display (INTENA/INTREQ/DMACON apagados e interrupciones del sistema
	/// congeladas). Las instalaciones posteriores son solo swaps de puntero.
	bool m_display_taken = false;
};

} // namespace eng::amiga
