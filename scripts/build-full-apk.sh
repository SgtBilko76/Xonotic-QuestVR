#!/bin/bash
# Build the self-contained XonoticQuest release APK: lean gradle build, then append the
# game data with python (gradle OOMs packaging 1.1GB of assets on this machine),
# zipalign and re-sign with the debug keystore.
# Expects the game data in $DIST/bundled-assets/{key_0.d0pk,gamedata/*.pk3}.
# DIST defaults to the dist/ folder next to the XonoticQuest project.
set -e
PROJ=$(cd "$(dirname "$0")/.." && pwd)
DIST=${DIST:-$(cd "$PROJ/.." && pwd)/dist}
BT=${ANDROID_HOME:-$HOME/Android/Sdk}/build-tools/34.0.0
LEAN=$PROJ/app/build/outputs/apk/release/app-release.apk
OUT=$DIST/XonoticQuest-full-release.apk
TMP=$DIST/.full-unsigned.apk
mkdir -p "$DIST/.javatmp"   # apksigner needs ~1.2GB of temp space; /tmp is a small tmpfs

( cd "$PROJ" && gradle assembleRelease --no-daemon -q )
python3 - "$LEAN" "$TMP" "$DIST/bundled-assets" <<'PY'
import sys, zipfile, os
lean, out, assets = sys.argv[1:]
with zipfile.ZipFile(lean) as zin, zipfile.ZipFile(out, 'w') as zout:
    for zi in zin.infolist():
        if zi.filename.startswith('META-INF/'):
            continue
        with zin.open(zi) as src, zout.open(zi, 'w') as dst:
            while True:
                b = src.read(1 << 20)
                if not b: break
                dst.write(b)
    zout.write(os.path.join(assets, 'key_0.d0pk'), 'assets/key_0.d0pk', zipfile.ZIP_STORED)
    for n in sorted(os.listdir(os.path.join(assets, 'gamedata'))):
        zout.write(os.path.join(assets, 'gamedata', n), 'assets/gamedata/' + n, zipfile.ZIP_STORED)
PY
rm -f "$OUT"
"$BT/zipalign" -p 4 "$TMP" "$OUT"
rm -f "$TMP"
JAVA_TOOL_OPTIONS="-Djava.io.tmpdir=$DIST/.javatmp" "$BT/apksigner" sign --ks "$PROJ/debug.keystore" --ks-pass pass:android --ks-key-alias androiddebugkey --key-pass pass:android --min-sdk-version 26 "$OUT"
"$BT/apksigner" verify "$OUT"
ls -la "$OUT"
