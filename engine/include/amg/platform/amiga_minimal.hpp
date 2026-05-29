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

#include <amg/core/types.hpp>
#include <amg/memory/arena.hpp>

namespace amg::amiga {

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

	/// Instala una copperlist ya construida en Chip RAM.
	///
	/// La lista debe terminar en `0xffff, 0xfffe`. Esta funcion escribe COP1LC,
	/// dispara COPJMP1 y activa DMA master + Copper. Es close-to-the-metal: el
	/// sistema operativo no arbitra esta lista.
	void install_copper_list(const u16* copper_words);

	/// Activa/desactiva warp mode del emulador mediante la ayuda de WinUAE-DBG.
	void set_warpmode(bool enabled);

	constexpr const Profile& profile() const { return m_profile; }
	constexpr MemorySystem& memory() { return m_memory; }
	constexpr const MemorySystem& memory() const { return m_memory; }
	constexpr const MemoryReport& memory_report() const { return m_memory_report; }
	constexpr DebugOverlay& debug() { return m_debug; }

private:
	Profile m_profile;
	MemorySystem m_memory {};
	MemoryReport m_memory_report {};
	DebugOverlay m_debug {};
	void* m_chip_alloc = nullptr;
	u32 m_chip_alloc_size = 0;
	void* m_slow_alloc = nullptr;
	u32 m_slow_alloc_size = 0;
	void* m_frame_alloc = nullptr;
	u32 m_frame_alloc_size = 0;
};

} // namespace amg::amiga
