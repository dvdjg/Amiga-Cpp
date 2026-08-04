#pragma once

/// \file run_status.hpp
/// Senal de vida legible desde el host durante pruebas automatizadas.
///
/// Las capturas de pantalla son imprescindibles para validar resultados visuales,
/// pero no deben ser el unico mecanismo para saber si una demo ha arrancado. Esta
/// estructura vive en memoria Amiga con un simbolo global conocido
/// (`g_eng_run_status`). El runner resuelve ese simbolo desde el mapa/ELF y lo lee
/// por GDB para distinguir rapidamente entre:
///
/// - la demo todavia no ha inicializado;
/// - la demo ha entrado en `init`;
/// - la demo ya esta renderizando frames;
/// - la demo fallo de forma controlada.
///
/// Esto mantiene los timeouts cortos: si una demo no alcanza `Ready` en pocos
/// segundos tras `continue`, el problema es real y no una captura tomada demasiado
/// pronto.

#include <eng/core/types.hpp>

extern "C" void eng_debug_ready_probe();

namespace eng::debug {

constexpr u32 run_status_magic = 0x454e4752u; // "ENGR"
constexpr u16 run_status_version = 1;

enum class RunState : u16 {
	Cold = 0,
	Booted = 1,
	InitStarted = 2,
	Ready = 3,
	Failed = 0xffff,
};

/// Estado minimo compartido entre la demo y el runner.
///
/// Todos los campos son escalares de tamano fijo para que el script de host pueda
/// decodificarlos sin depender de ABI C++ compleja. En 68000 los valores se guardan
/// big-endian; el runner los lee explicitamente asi.
struct RunStatus {
	u32 magic;
	u16 version;
	u16 state;
	u32 frame;
	u32 detail;
};

inline void reset(volatile RunStatus& status) {
	status.magic = run_status_magic;
	status.version = run_status_version;
	status.state = static_cast<u16>(RunState::Booted);
	status.frame = 0;
	status.detail = 0;
}

inline void mark_init_started(volatile RunStatus& status) {
	status.state = static_cast<u16>(RunState::InitStarted);
}

inline void mark_ready(volatile RunStatus& status, u32 detail = 0) {
	status.state = static_cast<u16>(RunState::Ready);
	status.detail = detail;
}

inline void mark_failed(volatile RunStatus& status, u32 detail) {
	status.state = static_cast<u16>(RunState::Failed);
	status.detail = detail;
}

inline void mark_frame(volatile RunStatus& status, u32 frame) {
	status.frame = frame;
}

inline void probe_when_ready(volatile RunStatus& status, u32 frame, u32 min_frame = 3) {
	if (status.state == static_cast<u16>(RunState::Ready) && frame >= min_frame) {
		eng_debug_ready_probe();
	}
}

} // namespace eng::debug
