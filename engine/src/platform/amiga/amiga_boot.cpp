/// \file amiga_boot.cpp
/// **Arranque de plataforma Amiga**: inicializa `SysBase` (la base de Exec que usan las
/// cabeceras `proto/` de Kickstart) desde el vector de excepción 4, **antes de `main`**.
///
/// La definición es *weak*: las demos antiguas que declaran su propio `SysBase` la sustituyen
/// (símbolo fuerte) y el constructor escribe igualmente en esa instancia; las demos nuevas no
/// declaran nada. El constructor corre en el arranque del soporte (`eng_c_start` ejecuta
/// `__init_array` antes de `main`, `support/gcc8_c_support.c`), así que `SysBase` está listo
/// para cualquier uso de Exec (AllocMem, Wait, …) en los constructores y en `main`.
///
/// Ceder esto al engine evita que cada aplicación repita el patrón
/// `struct ExecBase* SysBase = *reinterpret_cast<struct ExecBase**>(4UL);`, que es un detalle
/// de plataforma, no de juego (criterio de ergonomía: `CODING_STYLE.md` §Criterios del usuario).

#include <eng/debug/run_status.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

/// Definición débil: un `SysBase` fuerte de una demo antigua gana; si no, esta vale.
__attribute__((weak)) struct ExecBase* SysBase = nullptr;

extern "C" {
/// **RunStatus por defecto** (débil): las demos que ya definen el suyo lo sustituyen; las
/// nuevas no lo declaran. El runner resuelve este símbolo desde el `.map`.
__attribute__((weak)) volatile ::eng::debug::RunStatus g_eng_run_status {
	::eng::debug::run_status_magic,
	::eng::debug::run_status_version,
	static_cast<::eng::u16>(::eng::debug::RunState::Cold),
	0u,
	0u,
};
}

namespace {

/// Inicializa `SysBase` antes de `main` (init_array del soporte).
struct AmigaBoot {
	AmigaBoot() noexcept {
		SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	}
};

[[gnu::used]] AmigaBoot g_amiga_boot {};

} // namespace
