#!/bin/sh

# Runs listing_benchmark. Cold samples remount an ext4 image before every listing, which discards that filesystem's
# cache and leaves the rest of the machine warm; they need root, warm runs do not.
# What to verify before trusting the numbers: doc/testing.md, "Listing benchmark".
# The Qt kit is resolved the way run_tests.sh resolves it: QT_ROOT_DIR, then a git-ignored local-env.sh beside this
# script, then a system Qt.
# Arguments, the leading "debug" optional:
#   [debug] generate <folder>                 creates the benchmark folders under <folder>
#   [debug] warm <folder> [args...]           times every entries-* folder under <folder>
#   [debug] cold <image> <samples> [args...]  creates <image> if missing, generates the folders on it, then takes
#                                             <samples> cold samples of every folder and variant; needs root
#   remount <image> <mount point>             remounts <image> read-only at <mount point>: the benchmark runs this
#                                             before every cold listing
# Arguments after the command reach the benchmark, e.g. --variants qt,panel or --runs 30.

set -u

IMAGE_SIZE=512M
# The benchmark folders hold 111k entries: mkfs.ext4's default inode ratio gives this image 32k inodes
IMAGE_INODES=131072

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

fail()
{
	echo "$1" >&2
	exit 2
}

CONFIG=release
if [ "${1:-}" = debug ]; then
	CONFIG=debug
	shift
fi

COMMAND="${1:-}"
[ $# -gt 0 ] && shift

# Finds the loop devices by image: every remount is a separate run of this script
release_image()
{
	mountpoint -q "${MOUNT_POINT}" && umount "${MOUNT_POINT}"
	for device in $(losetup --noheadings --output NAME --associated "${IMAGE}"); do
		losetup --detach "${device}"
	done
}

# Direct I/O: the loop device otherwise reads the image through the page cache, which unmounting does not drop
# Arguments: ro or rw
mount_image()
{
	if [ "$1" = ro ]; then
		device="$(losetup --find --show --direct-io=on --read-only "${IMAGE}")" || exit 1
	else
		device="$(losetup --find --show --direct-io=on "${IMAGE}")" || exit 1
	fi
	mount -o "$1" "${device}" "${MOUNT_POINT}" || exit 1
}

if [ "${COMMAND}" = remount ]; then
	IMAGE="${1:-}"
	MOUNT_POINT="${2:-}"
	{ [ -n "${IMAGE}" ] && [ -n "${MOUNT_POINT}" ]; } || fail 'Usage: listing_benchmark.sh remount <image> <mount point>'

	release_image
	mount_image ro
	echo "${MOUNT_POINT}"
	exit 0
fi

BENCHMARK="${SCRIPT_DIR}/../bin/${CONFIG}/listing_benchmark"
[ -x "${BENCHMARK}" ] || fail "${BENCHMARK} is not built: run_tests.sh build builds it."

# The benchmark needs the Qt libraries; a system Qt needs nothing added
[ -z "${QT_ROOT_DIR:-}" ] && [ -f "${SCRIPT_DIR}/local-env.sh" ] && . "${SCRIPT_DIR}/local-env.sh"
if [ -n "${QT_ROOT_DIR:-}" ]; then
	LD_LIBRARY_PATH="${QT_ROOT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
	export LD_LIBRARY_PATH
fi

case "${COMMAND}" in
generate)
	FOLDER="${1:-}"
	[ -n "${FOLDER}" ] || fail 'Usage: listing_benchmark.sh generate <folder>'
	shift

	mkdir -p "${FOLDER}" || exit 1
	"${BENCHMARK}" --generate "${FOLDER}" "$@" || exit 1
	;;

warm)
	FOLDER="${1:-}"
	[ -n "${FOLDER}" ] || fail 'Usage: listing_benchmark.sh warm <folder> [args...]'
	shift

	# The folders go last: the benchmark takes them as positional arguments
	for folder in "${FOLDER}"/entries-*; do
		[ -d "${folder}" ] || fail "${FOLDER} has no benchmark folders: generate creates them."
		set -- "$@" "${folder}"
	done

	"${BENCHMARK}" "$@" || exit 1
	;;

cold)
	IMAGE="${1:-}"
	SAMPLES="${2:-}"
	{ [ -n "${IMAGE}" ] && [ -n "${SAMPLES}" ]; } || fail 'Usage: listing_benchmark.sh cold <image> <samples> [args...]'
	shift 2

	[ "$(id -u)" = 0 ] || fail 'Cold samples need root: mounting and unmounting the image is privileged.'
	command -v mkfs.ext4 >/dev/null 2>&1 || fail 'mkfs.ext4 is needed to create the image; it comes with e2fsprogs.'

	MOUNT_POINT="$(mktemp -d)"
	trap 'release_image; rmdir "${MOUNT_POINT}" 2>/dev/null; exit 1' INT TERM
	trap 'release_image; rmdir "${MOUNT_POINT}" 2>/dev/null' EXIT

	if [ ! -f "${IMAGE}" ]; then
		mkdir -p "$(dirname "${IMAGE}")" || exit 1
		truncate -s "${IMAGE_SIZE}" "${IMAGE}" || exit 1
		mkfs.ext4 -q -N "${IMAGE_INODES}" "${IMAGE}" || exit 1
	fi

	mount_image rw
	# Folders that already exist are kept, so an interrupted run can simply be repeated
	"${BENCHMARK}" --generate "${MOUNT_POINT}" || exit 1

	# The folder names go last: the benchmark takes them as positional arguments, relative to the mount point
	for folder in "${MOUNT_POINT}"/entries-*; do
		[ -d "${folder}" ] || fail "${IMAGE} has no benchmark folders."
		set -- "$@" "$(basename "${folder}")"
	done
	release_image

	"${BENCHMARK}" --cold "${SAMPLES}" --remount="${SCRIPT_DIR}/listing_benchmark.sh" --remount=remount --remount="${IMAGE}" --remount="${MOUNT_POINT}" "$@" || exit 1
	;;

*)
	fail 'Usage: listing_benchmark.sh [debug] generate <folder> | warm <folder> [args...] | cold <image> <samples> [args...]'
	;;
esac
