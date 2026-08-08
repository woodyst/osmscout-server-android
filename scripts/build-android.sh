#!/bin/bash
# Compila el APK de OSM Scout Server para Android.
#
# Antes hay que tener cruzadas las dependencias:
#   ./build-valhalla-android.sh all
#   ./build-microhttpd-android.sh
set -euo pipefail

QT_ANDROID=$HOME/Qt/6.8.3/android_arm64_v8a
QT_HOST=$HOME/Qt/6.8.3/gcc_64
NDK=$HOME/android-sdk/ndk/26.1.10909125
SDK=$HOME/android-sdk
SRC=${SRC:-$HOME/osmscout_android}
BUILD=$SRC/build-android
JOBS=${JOBS:-8}

"$QT_ANDROID/bin/qt-cmake" -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DQT_HOST_PATH="$QT_HOST" \
    -DANDROID_SDK_ROOT="$SDK" \
    -DANDROID_NDK_ROOT="$NDK" \
    -DQT_ANDROID_ABIS=arm64-v8a

cmake --build "$BUILD" -j"$JOBS"
cmake --build "$BUILD" --target apk

find "$BUILD" -name "*.apk" -newer "$BUILD/CMakeCache.txt" | head
