#!/bin/sh

set -e

MYSELF="$(basename $0)"

VOL="FileCommander"

APPDIR="FileCommander.app"

QTPATH=$1

rm -rf bin
echo "${MYSELF}: Building the app"
${QTPATH}/bin/qmake -spec macx-clang -r -config release CONFIG+=release "DEFINES+=NDEBUG"
make -j

echo "${MYSELF}: deploying Qt frameworks"
${QTPATH}/bin/macdeployqt bin/release/x64/${APPDIR}

echo "${MYSELF}: creating DMG"

cd bin/release/x64

DMG="${VOL}.dmg"
TMP_DMG="tmp_$$_${DMG}"

# create temporary image
hdiutil create "${TMP_DMG}" -ov -fs "HFS+" -volname "${VOL}" -size 200m
hdiutil attach "${TMP_DMG}"

cp -R ./${APPDIR} /Volumes/${VOL}/
ln -s /Applications /Volumes/${VOL}/

hdiutil detach "/Volumes/${VOL}"
hdiutil resize "${TMP_DMG}" -size min
hdiutil attach "${TMP_DMG}"

echo '
tell application "Finder"
  tell disk "'${VOL}'"
    open
    tell container window
      set current view to icon view
      set toolbar visible to false
      set statusbar visible to false
      set the bounds to {400, 100, 899, 356}
      set statusbar visible to false
    end tell

    set opts to the icon view options of container window
    tell opts
      set icon size to 72
      set arrangement to not arranged
    end tell

    delay 5
    set position of item "Applications" of container window to {400, 90}
    set position of item "'${APPDIR}'" of container window to {100, 90}
    delay 5
    eject
  end tell
end tell
' | osascript

#convert to compressed image, delete temp image
rm -f "$DMG"
hdiutil convert "${TMP_DMG}" -format UDZO -o "${DMG}"
rm -f "${TMP_DMG}"
mv "${DMG}" ../../../

echo "${MYSELF}: ready for distribution: ${DMG}"
