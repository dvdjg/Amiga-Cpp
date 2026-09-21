#pragma once

/// \file file.hpp
/// **E/S asíncrona del mini-SO** (`eng::os`): API de ficheros (crear/abrir/leer/escribir/borrar/
/// renombrar) y la notificación por mensaje (`FileDone`/`FileError`). La **implementación** la
/// aporta el backend (`dos.library`/`trackdisk`); aquí está el contrato y los tipos. Ver
/// `docs/engine/architecture/MINI_OS_IO.md` y `RESOURCE_SYSTEM.md`.
///
/// El buffer va como **vista** (`Span`) y el cookie discrimina al consumidor (`IoUser::tag`):
/// caché de assets, loader de código o stream.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/os/port.hpp>

namespace eng::os {

/// Modo de apertura.
enum class FileMode : eng::u8 { Read = 0, Write, ReadWrite, Create };

/// Operación (para el payload del mensaje).
enum class FileOp : eng::u8 { Read = 0, Write, Seek, Create, Delete, Rename };

using FileHandle = eng::u16; ///< 0 = inválido

/// **Cookie de E/S**: quién pidió la operación y con qué id. Se empaqueta en `u32`.
struct IoUser {
	eng::u8 tag = 0; ///< 'A' asset (caché), 'L' lib (loader), 'S' stream
	eng::u16 id = 0;

	/// Empaqueta `tag` + `id` en el cookie de 32 bits.
	[[nodiscard]] constexpr eng::u32 encode() const noexcept {
		return (static_cast<eng::u32>(tag) << 16u) | id;
	}
	/// Desempaqueta un cookie de 32 bits en `tag` + `id`.
	[[nodiscard]] static constexpr IoUser decode(eng::u32 cookie) noexcept {
		return IoUser {static_cast<eng::u8>(cookie >> 16u),
			       static_cast<eng::u16>(cookie & 0xffffu)};
	}
};

/// Notificación de una operación: mensaje al puerto (prioridad baja) y cookie del llamador.
struct IoNotify {
	bool post_message = true;
	MsgPrio prio = MsgPrio::Low;
	eng::u32 cookie = 0; ///< `IoUser::encode()`
};

/// Payload de `FileDone`/`FileError`.
struct FileDonePayload {
	FileHandle handle = 0;
	eng::s32 result = 0; ///< bytes transferidos o código <0
	eng::u8 op = 0;
	eng::u32 cookie = 0;

	[[nodiscard]] IoUser user() const noexcept { return IoUser::decode(cookie); }
};

// --- API (la implementa el backend) ------------------------------------------------------
FileHandle file_open(const char* path, FileMode mode);
void file_close(FileHandle h);
bool file_delete(const char* path);
bool file_rename(const char* old_path, const char* new_path);

/// Asíncrona: vuelve al momento; el resultado llega por `FileDone`/`FileError`.
bool file_read_async(FileHandle h, eng::Span<eng::u8> dst, eng::u32 offset,
		     const IoNotify& n = {});
bool file_write_async(FileHandle h, eng::Span<const eng::u8> src, eng::u32 offset,
		      const IoNotify& n = {});

/// Síncrona (bloquea): solo herramientas o pantallas de carga.
eng::s32 file_read_sync(FileHandle h, eng::Span<eng::u8> dst, eng::u32 offset);
eng::s32 file_write_sync(FileHandle h, eng::Span<const eng::u8> src, eng::u32 offset);

/// Crea un directorio (y sus padres si el backend lo permite). `false` si falla.
bool file_make_dir(const char* path);

/// Ejecuta **una** operación asíncrona pendiente y postea su `FileDone`/`FileError`. La llama el
/// bucle (o el tick). Devuelve `true` si ejecutó algo.
bool file_pump();

eng::u32 file_size(FileHandle h);
bool file_is_busy(FileHandle h);

} // namespace eng::os
