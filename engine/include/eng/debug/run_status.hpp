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

/// Telemetría por frame del coste de render, para supervisar que la carga de
/// CPU/Blitter/bus es homogénea frame a frame (sin picos).
///
/// La expone un símbolo global (`g_eng_frame_telemetry`) que el host puede leer
/// por el canal lateral de WinUAE-DBG (`mem <addr> <len>`, resolviendo el
/// símbolo desde el `.map`). Es un contrato fijo (sin ABI C++ compleja):
///
///   offset 0  frame       frame_index del último update
///   offset 4  blit_jobs   nº de blits encolados este frame
///   offset 6  blit_words  words de Blitter este frame (todos los planos)
///   offset 8  copper_words words de la copperlist emitida
///   offset 10 fillup_extra blits extra del ajuste de fillup (picos esperados)
///
/// El runner/tools pueden usar `g_eng_run_status.frame` y este bloque para
/// detectar si un frame dispara (jobs>baseline) o si hay micro-parones.
struct FrameTelemetry {
	u32 frame;         // 0
	u16 blit_jobs;     // 4
	u16 blit_words;    // 6
	u16 copper_words;  // 8
	u16 fillup_extra;  // 10
	u16 reserved[3];   // 12..18 (cero)
};

inline void reset(volatile FrameTelemetry& t) {
	t.frame = 0;
	t.blit_jobs = 0;
	t.blit_words = 0;
	t.copper_words = 0;
	t.fillup_extra = 0;
	t.reserved[0] = 0;
	t.reserved[1] = 0;
	t.reserved[2] = 0;
}

} // namespace eng::debug
