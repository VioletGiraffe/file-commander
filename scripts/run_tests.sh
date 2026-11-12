#!/bin/sh

# Builds file-commander-core/core-tests and runs every test executable. Exit code: 1 if any suite failed or was
# not built, 2 when the environment is incomplete.
# The Qt kit is found as resolve_qmake in private/qmake_build.sh describes.
# Arguments, all optional, in this order:
#   debug           build and run the debug configuration
#   build           only build; nobuild only runs what is already built, for a CI job that deploys in between
#   all            also run the slow tests that generate large files and trees, skipped by default
#   <suite> [args]  run only the suite whose executable name contains <suite>; the arguments after it go to
#                   that executable, e.g. a Catch2 test spec or --std-seed

set -u

TESTS="fso_test fso_test_high_level panel_test controller_test filesearchengine_test batchrename_test userprograms_test filesystemhelpers_test shell_test fileoperationlaunch_test fileoperations_test foldercomparison_test fileoperationsui_test fileoperations_gui_test filelist_test csvviewer_test tui_test"

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

if [ -n "${BUILD}" ]; then
	. "${SCRIPT_DIR}/private/qmake_build.sh"
	resolve_qmake
	build_qmake_project "${SCRIPT_DIR}/../file-commander-core/core-tests/core-tests.pro" "${CONFIG}" || exit 1
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
		foldercomparison_test) default_args="${FILECOMPARATOR_ARGS}" ;;
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
