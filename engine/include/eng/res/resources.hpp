#pragma once

/// \file resources.hpp
/// **Fachada de recursos** (`eng::res`): enruta los mensajes de E/S (`FileDone`/`FileError`) al
/// subsistema que los pidió, discriminando por `IoUser::tag` (caché de assets, loader de código o
/// stream). Así una sola cola sirve a todos los recursos sin cruzar consumidores. Ver
/// `docs/engine/architecture/RESOURCE_SYSTEM.md` §4.

#include <eng/os/file.hpp>
#include <eng/os/message.hpp>

namespace eng::res {

/// Etiquetas de `IoUser::tag`.
inline constexpr eng::u8 kTagAsset = 'A';
inline constexpr eng::u8 kTagLib = 'L';
inline constexpr eng::u8 kTagStream = 'S';

/// Enruta un mensaje de E/S. `Cache` debe ofrecer `on_load_done(eng::u16 id, eng::s32 result)`;
/// `Libs`, `on_file_done(eng::u16 id, eng::s32 result)`. Devuelve `true` si lo consumió.
template <class Cache, class Libs>
bool route_io(const eng::os::Msg& m, Cache& cache, Libs& libs) noexcept {
	if (m.type != eng::os::MsgType::FileDone && m.type != eng::os::MsgType::FileError) {
		return false;
	}
	const eng::os::IoUser u = eng::os::IoUser::decode(m.payload.file.cookie);
	switch (u.tag) {
	case kTagAsset:
		cache.on_load_done(u.id, m.payload.file.result);
		return true;
	case kTagLib:
		libs.on_file_done(u.id, m.payload.file.result);
		return true;
	default:
		return false; // 'S' stream u otros: los atiende su dueño
	}
}

} // namespace eng::res
