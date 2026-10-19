#!/bin/sh

set -e

MYSELF="$(basename $0)"

VOL="FileCommander"

APPDIR="FileCommander.app"

QTPATH=$1

ROOT_DIR="$(pwd)"

rm -rf bin
echo "${MYSELF}: Building the app"
${QTPATH}/bin/qmake -spec macx-clang -r -config release CONFIG+=release
make -j$(sysctl -n hw.ncpu)

echo "${MYSELF}: deploying Qt frameworks"
${QTPATH}/bin/macdeployqt bin/release/${APPDIR}

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
