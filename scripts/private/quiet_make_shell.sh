#!/bin/sh

# A make SHELL that prints a recipe line in full, with its stderr, only when it fails or writes to stderr.
# A clean compile prints only its source file name; any other clean line prints nothing.
# Usage: make -s SHELL=/path/to/quiet_make_shell.sh
# Reports go to stdout: a recursive make line buffers its stderr, so a sub-make's report there would arrive late.

shell_flags=$1
recipe=$2

stderr_file=$(mktemp) || exit
/bin/sh "${shell_flags}" "${recipe}" 2>"${stderr_file}"
status=$?

if [ ${status} -ne 0 ] || [ -s "${stderr_file}" ]; then
	printf '%s\n' "${recipe}"
	cat "${stderr_file}"
else
	case "${recipe}" in
		*" -c "*.cpp | *" -c "*.c | *" -c "*.mm | *" -c "*.m) printf '%s\n' "${recipe##*[ /]}" ;;
	esac
fi

rm -f "${stderr_file}"
exit ${status}
