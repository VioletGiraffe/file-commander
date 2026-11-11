#!/bin/sh

# Sourced by the scripts that build with qmake.
# Requires SCRIPT_DIR, the scripts folder: a sourced script cannot find its own path.

# Sets QMAKE, or exits with code 2. The Qt kit is resolved in order: QT_ROOT_DIR as already set (what
# jurplel/install-qt-action exports on CI); scripts/local-env.sh, git-ignored, where a developer sets it for their
# machine; a qmake6 or qmake already on PATH (a distribution's Qt).
resolve_qmake()
{
	[ -z "${QT_ROOT_DIR:-}" ] && [ -f "${SCRIPT_DIR}/local-env.sh" ] && . "${SCRIPT_DIR}/local-env.sh"

	if [ -n "${QT_ROOT_DIR:-}" ]; then
		QMAKE="${QT_ROOT_DIR}/bin/qmake"
		if [ ! -x "${QMAKE}" ]; then
			echo "No qmake under \"${QT_ROOT_DIR}/bin\"." >&2
			exit 2
		fi
	elif command -v qmake6 >/dev/null 2>&1; then
		QMAKE=qmake6
	elif command -v qmake >/dev/null 2>&1; then
		QMAKE=qmake
	else
		echo "QT_ROOT_DIR is not set and no qmake is on PATH. Set QT_ROOT_DIR to the Qt kit directory, the one holding bin/qmake, or set it in a git-ignored scripts/local-env.sh." >&2
		exit 2
	fi
}

# build_qmake_project <.pro file> <release|debug>: builds the project and its subprojects in the project's folder.
# Requires QMAKE. Fails with the failing step's status.
build_qmake_project()
(
	# A kept .qmake.stash pins the toolchain probed when it was written, so every run probes the current one instead.
	# One is written per subproject build directory, and qmake also searches upward, so the sweep covers the repository.
	find "${SCRIPT_DIR}/.." -name .qmake.stash -delete
	cd "$(dirname "$1")" &&
		"${QMAKE}" -r CONFIG+="$2" "$(basename "$1")" &&
		make -s -j"$(getconf _NPROCESSORS_ONLN)" SHELL="${SCRIPT_DIR}/private/quiet_make_shell.sh"
)
