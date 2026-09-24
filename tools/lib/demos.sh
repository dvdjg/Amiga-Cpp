#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Helper de descubrimiento de demos/tests del engine (fuente unica).
#
# Una "demo" es un directorio bajo demos/ (o tests/) que contiene `src/` con al
# menos un `.cpp`. Con la estructura techniques/features, la profundidad varia,
# asi que NO se puede asumir `demos/<plataforma>/<demo>/`.
#
# Uso (source): . tools/lib/demos.sh
#   list_demos [<raiz>]              -> rutas ABSOLUTAS de las demos bajo demos/ y tests/
#   list_demos_rel [<raiz>]          -> rutas RELATIVAS a la raiz, ordenadas
# ---------------------------------------------------------------------------

# Imprime las rutas absolutas de las demos (dirs con src/*.cpp bajo demos/).
list_demos() {
	local root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
	find "$root/demos" -type f -name '*.cpp' -path '*/src/*' -printf '%h\n' 2>/dev/null \
		| sed 's#/src$##' | sort -u
}

# Igual, pero incluye tambien los tests con fuente bajo tests/ (on-target).
list_targets() {
	local root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
	{
		find "$root/demos" -type f -name '*.cpp' -path '*/src/*' -printf '%h\n' 2>/dev/null
		find "$root/tests" -type f -name '*.cpp' -path '*/src/*' -printf '%h\n' 2>/dev/null
	} | sed 's#/src$##' | sort -u
}

# Imprime las rutas relativas a la raiz.
list_demos_rel() {
	local root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
	list_demos "$root" | sed "s#^$root/##"
}
