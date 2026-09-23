#pragma once

/// \file trackdisk.hpp
/// **Acceso a disco a nivel de device** (`eng::os`): abre `trackdisk.device` y lee/escribe
/// **sectores** por offset, sin pasar por el sistema de ficheros. Para streaming de datos,
/// bootblocks y formatos no-DOS; el fichero normal va por `eng::os::file_*` (dos.library).
///
/// El backend (Amiga) implementa el contrato con `OpenDevice("trackdisk.device", unit, ...)`,
/// `IOExtTD` y `DoIO`. Ver `docs/reference/emulators/winuae/trackdisk.md` (registros y DMA) y
/// `docs/engine/architecture/MINI_OS_IO.md`.
///
/// ```text
///   td_open(unit) ──► TdHandle ──► td_motor(on) ──► td_read_sync(offset, dst)
///        │                                              (dst DEBE ser Chip RAM)
///        └── td_change_state / td_change_num (¿hay disco? ¿cambió?)
/// ```
///
/// \warning La DMA de disco **solo ve Chip RAM**: `td_read_sync` con un buffer de Fast RAM
/// produce corrupción silenciosa. Reserva el buffer en la arena `chip` del backend.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::os {

using TdHandle = eng::u16; ///< 0 = inválido

/// Geometría estándar de un disquete DD (`trackdisk`): 80 pistas × 2 caras × 11 sectores.
struct TdGeometry {
	eng::u16 tracks = 80u;
	eng::u8 sides = 2u;
	eng::u8 sectors = 11u;
	eng::u16 sector_bytes = 512u;
};

/// Abre `trackdisk.device` para la unidad `unit` (`DF0:`=0, `DF1:`=1…). Devuelve el handle
/// (1..N) o 0 si no hay puerto/IORequest/device. Idempotente por unidad.
TdHandle td_open(eng::u16 unit);

/// Cierra la unidad y libera puerto e IORequest. Handle inválido → no hace nada.
void td_close(TdHandle h);

/// Enciende (`on`) o apaga el motor. Apágalo al terminar una ráfaga (desgaste del disco).
bool td_motor(TdHandle h, bool on);

/// Lee `dst.size()` bytes desde el offset **lineal en bytes** del medio
/// (`offset = (track*2 + side)*sectors*512 + sector*512`). `dst` debe estar en **Chip RAM**.
/// `false` si el handle no es válido o `DoIO` devuelve error (p. ej. sin disco).
bool td_read_sync(TdHandle h, eng::u32 byte_offset, eng::Span<eng::u8> dst);

/// Estado del disco: 0 = hay disco, 1 = bandeja vacía (`TD_CHANGESTATE`).
eng::u8 td_change_state(TdHandle h);

/// Contador de cambios de disco (`TD_CHANGENUM`); si cambia entre lecturas, el medio se
/// sustituyó y hay que releer. 0 si el handle no es válido.
eng::u32 td_change_num(TdHandle h);

} // namespace eng::os
