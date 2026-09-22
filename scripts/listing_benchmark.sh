#!/bin/sh

# Runs listing_benchmark. Cold samples unmount an ext4 image between listings, which discards that filesystem's
# cache and leaves the rest of the machine warm; they need root, warm runs do not.
# What to verify before trusting the numbers: doc/testing.md, "Listing benchmark".
# The Qt kit is resolved the way run_tests.sh resolves it: QT_ROOT_DIR, then a git-ignored local-env.sh beside this
# script, then a system Qt.
# Arguments, the leading "debug" optional:
#   [debug] generate <folder>                 creates the benchmark folders under <folder>
#   [debug] warm <folder> [args...]           times every entries-* folder under <folder>
#   [debug] cold <image> <samples> [args...]  creates <image> if missing, generates the folders on it, then takes
#                                             <samples> cold samples of every folder and variant; needs root
# Arguments after the command reach the benchmark, e.g. --runs 30; a repeated option takes its last value, so they
# win. VARIANTS holds the variants a cold run times one at a time, each in its own process.

set -u

VARIANTS="${VARIANTS:-qt qt-unsorted thinio panel}"
IMAGE_SIZE=512M

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

BENCHMARK="${SCRIPT_DIR}/../bin/${CONFIG}/listing_benchmark"
[ -x "${BENCHMARK}" ] || fail "${BENCHMARK} is not built: run_tests.sh build builds it."

# The benchmark needs the Qt libraries; a system Qt needs nothing added
[ -z "${QT_ROOT_DIR:-}" ] && [ -f "${SCRIPT_DIR}/local-env.sh" ] && . "${SCRIPT_DIR}/local-env.sh"
if [ -n "${QT_ROOT_DIR:-}" ]; then
	LD_LIBRARY_PATH="${QT_ROOT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
	export LD_LIBRARY_PATH
fi

CSV_DIR="$(dirname "${BENCHMARK}")"

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

	"${BENCHMARK}" --label "$(basename "${FOLDER}")" --csv "${CSV_DIR}/listing_benchmark_warm.csv" "$@" || exit 1
	echo "Results appended to ${CSV_DIR}/listing_benchmark_warm.csv"
	;;

cold)
	IMAGE="${1:-}"
	SAMPLES="${2:-}"
	{ [ -n "${IMAGE}" ] && [ -n "${SAMPLES}" ]; } || fail 'Usage: listing_benchmark.sh cold <image> <samples> [args...]'
	shift 2

	[ "$(id -u)" = 0 ] || fail 'Cold samples need root: mounting and unmounting the image is privileged.'
	command -v shuf >/dev/null 2>&1 || fail 'shuf is needed to shuffle the job order; it comes with coreutils.'
	command -v mkfs.ext4 >/dev/null 2>&1 || fail 'mkfs.ext4 is needed to create the image; it comes with e2fsprogs.'

	MOUNT_POINT="$(mktemp -d)"
	LOOP_DEVICE=""

	release_image()
	{
		mountpoint -q "${MOUNT_POINT}" && umount "${MOUNT_POINT}"
		[ -n "${LOOP_DEVICE}" ] && losetup -d "${LOOP_DEVICE}"
		LOOP_DEVICE=""
	}

	trap 'release_image; rmdir "${MOUNT_POINT}" 2>/dev/null; exit 1' INT TERM
	trap 'release_image; rmdir "${MOUNT_POINT}" 2>/dev/null' EXIT

	# Direct I/O: the loop device otherwise reads the image through the page cache, which unmounting does not drop
	mount_image_readonly()
	{
		LOOP_DEVICE="$(losetup --find --show --read-only --direct-io=on "${IMAGE}")" || exit 1
		mount -o ro "${LOOP_DEVICE}" "${MOUNT_POINT}" || exit 1
	}

	if [ ! -f "${IMAGE}" ]; then
		mkdir -p "$(dirname "${IMAGE}")" || exit 1
		truncate -s "${IMAGE_SIZE}" "${IMAGE}" || exit 1
		mkfs.ext4 -q "${IMAGE}" || exit 1
	fi

	mount -o loop "${IMAGE}" "${MOUNT_POINT}" || exit 1
	# Folders that already exist are kept, so an interrupted run can simply be repeated
	"${BENCHMARK}" --generate "${MOUNT_POINT}" || exit 1

	JOBS=""
	for folder in "${MOUNT_POINT}"/entries-*; do
		[ -d "${folder}" ] || fail "${IMAGE} has no benchmark folders."
		for variant in ${VARIANTS}; do
			JOBS="${JOBS} $(basename "${folder}"):${variant}"
		done
	done
	release_image

	COLD_CSV="${CSV_DIR}/listing_benchmark_cold.csv"
	LABEL="$(basename "${IMAGE}")"
	sample=1
	while [ "${sample}" -le "${SAMPLES}" ]; do
		echo "Sample ${sample} of ${SAMPLES}"
		# Each round is shuffled: the drive's own cache survives the remount, and must not favour the same jobs every time
		for job in $(printf '%s\n' ${JOBS} | shuf); do
			mount_image_readonly
			"${BENCHMARK}" --once --variants "${job#*:}" --label "${LABEL}" --csv "${COLD_CSV}" "$@" "${MOUNT_POINT}/${job%:*}" || exit 1
			release_image
		done
		sample=$((sample + 1))
	done

	echo "Results appended to ${COLD_CSV}"
	;;

*)
	fail 'Usage: listing_benchmark.sh [debug] generate <folder> | warm <folder> [args...] | cold <image> <samples> [args...]'
	;;
esac
