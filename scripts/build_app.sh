#!/bin/sh

# Builds File Commander, its plugins and the terminal UI into bin/<configuration>.
# Exit code: 1 if the build failed, 2 when the environment is incomplete.
# The Qt kit is found as resolve_qmake in private/qmake_build.sh describes.
# Argument, optional: debug, to build the debug configuration instead of release.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "${1:-}" in
	"") CONFIG=release ;;
	debug) CONFIG=debug ;;
	*)
		echo "Unknown argument \"$1\": the only one accepted is debug." >&2
		exit 2
		;;
esac

. "${SCRIPT_DIR}/private/qmake_build.sh"
resolve_qmake
build_qmake_project "${SCRIPT_DIR}/../file-commander.pro" "${CONFIG}" || exit 1
