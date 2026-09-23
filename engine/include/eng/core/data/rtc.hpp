#pragma once

/// \file rtc.hpp
/// Reloj de tiempo real a partir del contador **TOD** de la CIA-A: conversion pura,
/// freestanding y host-testable.
///
/// La CIA-A tiene un contador de **tiempo del dia (TOD)** de 24 bits que incrementa a
/// **50 Hz** (PAL, `CRA bit7 TODIN=1`) o **60 Hz** (NTSC). Se lee con el latch de la CIA:
/// leer `TODHI` congela `TODMID`/`TODLO`, asi que el orden es `TODHI -> TODMID -> TODLO
/// (ver el repo hermano `amiga-bootcamp/01_hardware/common/cia_chips.md`, "Time-of-Day").
///
/// Aqui solo vive la **conversion** (contador -> hora:minuto:segundo + ticks); la lectura
/// del registro la hace el backend (`MinimalBackend::cia_tod_ticks`).

#include <eng/core/types.hpp>

namespace eng::time {

/// Hora del dia derivada del contador TOD.
struct TimeOfDay {
	u8 hours = 0;
	u8 minutes = 0;
	u8 seconds = 0;
	u8 ticks = 0;         ///< subsegundo (0..hz-1)
	u8 hz = 50;           ///< frecuencia del TOD (50 PAL / 60 NTSC)
	u32 total_seconds = 0;
};

/// Convierte un contador TOD de 24 bits a hora del dia (modulo 24 h).
constexpr TimeOfDay from_tod(u32 tod, u8 hz = 50) {
	TimeOfDay t {};
	t.hz = hz;
	const u32 rate = (hz == 0u) ? 50u : hz;
	t.total_seconds = tod / rate;
	t.ticks = static_cast<u8>(tod % rate);
	t.seconds = static_cast<u8>(t.total_seconds % 60u);
	t.minutes = static_cast<u8>((t.total_seconds / 60u) % 60u);
	t.hours = static_cast<u8>((t.total_seconds / 3600u) % 24u);
	return t;
}

} // namespace eng::time
