#!/bin/sh

# Builds file-commander-core/core-tests and runs every test executable. Exit code: 1 if any suite failed or was
# not built, 2 when the environment is incomplete.
# The Qt kit is resolved in order: QT_ROOT_DIR as already set (what jurplel/install-qt-action exports on CI);
# local-env.sh beside this script, git-ignored, where a developer sets it for their machine; a qmake6 or qmake
# already on PATH (a distribution's Qt).
# Arguments, all optional, in this order:
#   debug           build and run the debug configuration
#   build           only build; nobuild only runs what is already built, for a CI job that deploys in between
#   all             also run the slow tests that generate large files and trees, skipped by default
#   <suite> [args]  run only the suite whose executable name contains <suite>; the arguments after it go to
#                   that executable, e.g. a Catch2 test spec or --std-seed

set -u

TESTS="fso_test fso_test_high_level panel_test filesearchengine_test userprograms_test filesystemhelpers_test fileoperations_test filecomparator_test fileoperations_gui_test filelist_test csvviewer_test"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

CONFIG=release
if [ "${1:-}" = debug ]; then
	CONFIG=debug
	shift
fi

BUILD=1
RUN=1
if [ "${1:-}" = build ]; then
	RUN=""
	shift
elif [ "${1:-}" = nobuild ]; then
	BUILD=""
	shift
fi

# Skipped by default: these generate trees of thousands of files and gigabytes of data, and CI runs them
# repeatedly with fresh seeds. Every other case in both suites still runs.
FILEOPERATIONS_ARGS="~[executor]~[deleteexecutor]"
FILECOMPARATOR_ARGS="~[CFileComparator]"
if [ "${1:-}" = all ]; then
	FILEOPERATIONS_ARGS=""
	FILECOMPARATOR_ARGS=""
	shift
fi

SUITE="${1:-}"
[ $# -gt 0 ] && shift

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
	echo "QT_ROOT_DIR is not set and no qmake is on PATH. Set QT_ROOT_DIR to the Qt kit directory, the one holding bin/qmake, or set it in a git-ignored local-env.sh beside this script." >&2
	exit 2
fi

if [ -n "${BUILD}" ]; then
	cd "${SCRIPT_DIR}/../file-commander-core/core-tests" || exit 2
	# A kept .qmake.stash pins the toolchain probed when it was written, so every run probes the current one instead.
	# One is written per subproject build directory, and qmake also searches upward, so the sweep covers the repository.
	find "${SCRIPT_DIR}/.." -name .qmake.stash -delete
	"${QMAKE}" -r CONFIG+="${CONFIG}" || exit 1
	make -j"$(getconf _NPROCESSORS_ONLN)" || exit 1
fi
[ -n "${RUN}" ] || exit 0

BIN="${SCRIPT_DIR}/../bin/${CONFIG}"
# The widget tests need the offscreen platform where there is no display server
if [ "$(uname -s)" = Linux ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	QT_QPA_PLATFORM=offscreen
	export QT_QPA_PLATFORM
fi

failed=""
ran=""
for test in ${TESTS}; do
	case "${test}" in
		*"${SUITE}"*) ;;
		*) continue ;;
	esac
	ran="${ran} ${test}"

	exe="${BIN}/${test}"
	[ -x "${exe}.app/Contents/MacOS/${test}" ] && exe="${exe}.app/Contents/MacOS/${test}"
	if [ ! -x "${exe}" ]; then
		echo "${test}: not built"
		failed="${failed} ${test}"
		continue
	fi

	default_args=""
	case "${test}" in
		fileoperations_test) default_args="${FILEOPERATIONS_ARGS}" ;;
		filecomparator_test) default_args="${FILECOMPARATOR_ARGS}" ;;
	esac

	echo
	echo "===== ${test}"
	# --warn NoTests: a filter that matches nothing exits 0 otherwise, so a broken one would pass silently
	if [ $# -gt 0 ]; then
		"${exe}" "$@" --warn NoTests || failed="${failed} ${test}"
	elif [ -n "${default_args}" ]; then
		"${exe}" "${default_args}" --warn NoTests || failed="${failed} ${test}"
	else
		"${exe}" --warn NoTests || failed="${failed} ${test}"
	fi
done

if [ -z "${ran}" ]; then
	echo "No test executable matches \"${SUITE}\"." >&2
	exit 2
fi
if [ -n "${failed}" ]; then
	echo
	echo "FAILED:${failed}"
	exit 1
fi
echo
echo "All suites passed:${ran}"
