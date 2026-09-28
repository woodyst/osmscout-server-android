#!/bin/bash
# Sincroniza el arbol local a la maquina de compilacion. Se ejecuta EN local.
#
# La SBC local no aguanta compilar esto: son Valhalla, libpostal, protobuf y el
# servidor entero en C++. Por defecto va a ia (32 nucleos, 93 GB); erebos3 sigue
# valiendo y tiene el mismo arbol, asi que se le pasa como argumento:
#     ./sync-build.sh erebos3
#
# Lo que NO viaja por aqui y tiene que estar ya en la maquina. Son horas de
# compilacion cruzada, asi que a ia se le copiaron de erebos3 el 28/09/2026 con
# `ssh erebos3 tar czf - ... | ssh ia tar xzf - ...`, que ia y erebos3 no se ven
# entre si. Van en este orden porque es el orden en que las echa de menos el
# compilador, una por una:
#
#   ~/src_valhalla/install-android-arm64          189 MB. Las dependencias
#                                                 cruzadas: microhttpd, sqlite,
#                                                 protobuf, libpostal, marisa,
#                                                 kyotocabinet, bzip2.
#   ~/src_valhalla/boost-headers                  181 MB. Solo cabeceras, y en un
#                                                 arbol que contiene SOLO boost/,
#                                                 que es como lo quiere el
#                                                 -isystem de CMakeLists.
#   ~/pkg-valhalla-lite/valhalla                  272 MB de fuentes, por las
#                                                 cabeceras.
#   ~/pkg-valhalla-lite/valhalla/build-android/   de aqui hacen falta las 25
#     src/*.h y src/libvalhalla.so                cabeceras generadas y la
#                                                 biblioteca, 130 MB.
#   ~/android_openssl/ssl_3/arm64-v8a             libcrypto_3.so y libssl_3.so,
#                                                 que van dentro del APK.
#   ~/navius-keys/osmscout-release.jks            la clave de firma, con su
#                                                 pass-osmscout.txt. Fuera del
#                                                 arbol a proposito, para que no
#                                                 la arrastre este rsync.
#   ~/Qt/6.8.3/android_arm64_v8a                  Qt para Android.
#   ~/android-sdk/ndk/26.1.10909125               el NDK.
#
# Comprobado el 28/09/2026: con todo eso, ia produce un APK byte a byte del mismo
# tamaño que el de erebos3.
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
