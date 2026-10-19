#!/bin/sh

set -e

MYSELF="$(basename $0)"

VOL="FileCommander"

APPDIR="FileCommander.app"
BUNDLE="bin/release/${APPDIR}"
EXECUTABLE="${BUNDLE}/Contents/MacOS/FileCommander"

QTPATH=$1

ROOT_DIR="$(pwd)"

rm -rf bin
echo "${MYSELF}: Building the app"
${QTPATH}/bin/qmake -spec macx-clang -r -config release CONFIG+=release
make -s -j$(sysctl -n hw.ncpu) SHELL="${ROOT_DIR}/scripts/quiet_make_shell.sh"

echo "${MYSELF}: deploying Qt frameworks"
# macdeployqt exits 0 even when it deploys nothing: its ERROR lines count as a failure too
DEPLOY_STATUS=0
DEPLOY_OUTPUT="$("${QTPATH}/bin/macdeployqt" "${BUNDLE}" 2>&1)" || DEPLOY_STATUS=$?
echo "${DEPLOY_OUTPUT}"
if [ ${DEPLOY_STATUS} -ne 0 ] || echo "${DEPLOY_OUTPUT}" | grep -q "^ERROR"; then
	echo "${MYSELF}: macdeployqt failed, refusing to package an undeployed bundle" >&2
	exit 1
fi

# The Qt kit is universal: macdeployqt copies slices for every architecture, and the app loads only its own
APP_ARCHS="$(lipo -archs "${EXECUTABLE}")"
case "${APP_ARCHS}" in
	*" "*)
		echo "${MYSELF}: the app is universal (${APP_ARCHS}), keeping every slice"
		;;
	*)
		echo "${MYSELF}: thinning the bundle to ${APP_ARCHS}"
		find "${BUNDLE}" -type f | while IFS= read -r FILE; do
			if lipo -archs "${FILE}" 2>/dev/null | grep -q " "; then
				lipo "${FILE}" -thin "${APP_ARCHS}" -output "${FILE}"
			fi
		done
		;;
esac

# Thinning invalidates the ad-hoc signatures macdeployqt applied: Apple Silicon kills arm64 code without a valid one at load.
# Ad-hoc ("-") needs no certificate.
echo "${MYSELF}: re-signing ${BUNDLE} ad-hoc"
codesign --force --deep --sign - "${BUNDLE}"
if ! codesign --verify --deep --strict "${BUNDLE}"; then
	echo "${MYSELF}: ${BUNDLE} fails signature verification, refusing to package it" >&2
	exit 1
fi

echo "${MYSELF}: creating DMG"

cd bin/release

DMG="${VOL}.dmg"
STAGE="dmg-staging"

rm -rf "${STAGE}"
mkdir "${STAGE}"
cp -R "./${APPDIR}" "${STAGE}/"
ln -s /Applications "${STAGE}/"

# -srcfolder populates via a private nobrowse mount - no volume appears under /Volumes for Spotlight to grab and pin.
# Retried: on CI runners hdiutil still fails intermittently with "Resource busy".
ATTEMPT=1
until hdiutil create "${DMG}" -ov -volname "${VOL}" -fs "HFS+" -format UDZO -srcfolder "${STAGE}"; do
	[ ${ATTEMPT} -lt 3 ] || exit 1
	ATTEMPT=$((ATTEMPT + 1))
	echo "${MYSELF}: hdiutil failed, retrying (attempt ${ATTEMPT})"
	sleep 10
done

rm -rf "${STAGE}"
mv "${DMG}" "${ROOT_DIR}/"

echo "${MYSELF}: ready for distribution: ${DMG}"
