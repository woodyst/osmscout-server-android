#!/bin/bash
# Sincroniza el arbol local a la maquina de compilacion. Se ejecuta EN local.
#
# La SBC local no aguanta compilar esto: son Valhalla, libpostal, protobuf y el
# servidor entero en C++. Por defecto va a ia (32 nucleos, 93 GB); erebos3 sigue
# valiendo y tiene el mismo arbol, asi que se le pasa como argumento:
#     ./sync-build.sh erebos3
#
# Lo que NO viaja por aqui y tiene que estar ya en la maquina:
#
#   ~/src_valhalla/install-android-arm64   las dependencias cruzadas (189 MB).
#                                          Se generan con los scripts
#                                          build-valhalla-android.sh y compania,
#                                          que tardan horas; se copiaron de
#                                          erebos3 a ia el 28/09/2026.
#   ~/navius-keys/osmscout-release.jks     la clave de firma, con su pass-*.txt.
#                                          Fuera del arbol a proposito, para que
#                                          no la arrastre este rsync.
#   ~/Qt/6.8.3/android_arm64_v8a           Qt para Android.
#   ~/android-sdk/ndk/26.1.10909125        el NDK.
set -euo pipefail

HOST=${1:-${OSMSCOUT_BUILD_HOST:-ia}}

SRC_DIR=$(cd "$(dirname "$0")" && pwd)

rsync -a --delete \
    --exclude='.git/' \
    --exclude='build-android/' \
    --exclude='build-android-release/' \
    --exclude='local.properties' \
    --exclude='*.apk' \
    "$SRC_DIR/" "$HOST:~/osmscout_android/"

echo "sincronizado -> $HOST:~/osmscout_android/"
