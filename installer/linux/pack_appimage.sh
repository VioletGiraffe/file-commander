#!/bin/sh
# Packs an AppDir that linuxdeploy deployed into an AppImage: pack_appimage.sh <AppDir> <output file>
# Runs appimagetool directly: linuxdeploy's AppImage plugin passes it no mksquashfs options.
# zstd level 19 with 1 MB blocks, squashfs's maximum: about 9% smaller than appimagetool's 128 KB blocks at the default level.
# appimagetool chose 128 KB for startup time (its issue #64); the smaller download is preferred here.
# Expects appimagetool-<arch>.AppImage in the current folder.
set -e
arch=$(uname -m)
ARCH=$arch "./appimagetool-$arch.AppImage" --comp zstd \
	--mksquashfs-opt=-Xcompression-level --mksquashfs-opt=19 --mksquashfs-opt=-b --mksquashfs-opt=1M \
	"$1" "$2"
