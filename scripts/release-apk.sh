#!/bin/bash
# Compila el APK de release y lo firma con nuestra clave. Se ejecuta EN la
# maquina de compilacion, igual que build-android.sh.
#
# Este servidor NO va por Play todavia: se reparte como descarga directa, para
# instalar desde origen externo. Por eso se firma un APK y no un AAB, y por eso
# la firma importa mas de lo que parece: Android identifica la aplicacion por su
# firma, asi que **todas las versiones futuras tienen que ir con esta misma
# clave**. Si cambia, quien lo tenga instalado no puede actualizar: tiene que
# desinstalar, y al desinstalar Android borra /Android/data/<paquete>, o sea
# LOS MAPAS DESCARGADOS, que son varios gigas.
#
# La clave vive FUERA del arbol, en ~/navius-keys/osmscout-release.jks, para que
# no entre en git ni la arrastre ningun rsync. Esta en erebos3 y en ia, con
# permisos 600. Tener dos copias es a proposito: es el unico fichero del
# proyecto que no se puede volver a generar.
set -euo pipefail

QT_VERSION=${QT_VERSION:-6.8.3}
QT_ROOT=$HOME/Qt/$QT_VERSION
QT_HOST=$QT_ROOT/gcc_64
QT_ANDROID=$QT_ROOT/android_arm64_v8a

export ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-$HOME/android-sdk}
export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/${NDK_VERSION:-26.1.10909125}
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk-amd64}
# En erebos3 services.gradle.org solo resuelve a IPv6 y no hay ruta IPv6: la JVM
# del wrapper de Gradle muere con "La red es inaccesible". Forzar IPv4 no estorba
# donde si hay IPv6, asi que se deja siempre puesto.
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djava.net.preferIPv4Stack=true"
export GRADLE_OPTS="${GRADLE_OPTS:-} -Djava.net.preferIPv4Stack=true"

SRC_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-$SRC_DIR/build-android-release}
JOBS=${JOBS:-$(nproc)}

KEYSTORE=${KEYSTORE:-$HOME/navius-keys/osmscout-release.jks}
KEYALIAS=${KEYALIAS:-osmscout-release}
# La contrasena se lee de fichero o de la variable KEYPASS. Nunca del historial.
KEYPASS=${KEYPASS:-$(cat "$HOME/navius-keys/pass-osmscout.txt" 2>/dev/null || true)}

[ -f "$KEYSTORE" ] || { echo "ERROR: no esta la clave en $KEYSTORE"; exit 1; }
[ -n "$KEYPASS" ]  || { echo "ERROR: sin contrasena (KEYPASS o ~/navius-keys/pass-osmscout.txt)"; exit 1; }

# androiddeployqt COPIA a android-build/ pero no borra lo que ya no toca, y un
# android-build sucio produce paquetes que fallan de formas que no se parecen a
# la causa. Misma leccion que en Navius.
rm -rf "$BUILD_DIR/android-build"

"$QT_ANDROID/bin/qt-cmake" -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="$QT_HOST" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DQT_ANDROID_ABIS=arm64-v8a

cmake --build "$BUILD_DIR" -j"$JOBS"

cmake --build "$BUILD_DIR" --target apk -j"$JOBS"

# Se firma a mano, no con las variables QT_ANDROID_KEYSTORE_*. Esas solo hacen
# efecto si androiddeployqt se ejecuta de verdad, y si el objetivo `apk` ya esta
# al dia no se ejecuta: sale un APK sin firmar y el guion sigue tan tranquilo.
# Aqui, en cambio, se parte SIEMPRE del APK sin firmar y se firma, que ademas es
# el camino estandar de Android (zipalign y luego apksigner).
BT=$(ls -d "$ANDROID_SDK_ROOT"/build-tools/* | sort -V | tail -1)
SALIDA=$BUILD_DIR/android-build/build/outputs/apk/release
# Ruta EXACTA, no `find | head -1`: gradle deja varios .apk por ahi y el orden de
# find no es estable. En Navius eso llego a firmar el paquete de depuracion sin
# que nada avisara.
SIN_FIRMAR=$SALIDA/android-build-release-unsigned.apk
[ -f "$SIN_FIRMAR" ] || { echo "ERROR: no esta el APK sin firmar en $SIN_FIRMAR"; exit 1; }

VERSION=$(grep -oP 'project\(osmscout-server-android VERSION \K[0-9.]+' "$SRC_DIR/CMakeLists.txt")
DESTINO=$SRC_DIR/osmscout-server-$VERSION-arm64-v8a.apk

# zipalign ANTES de firmar: alinear despues invalidaria la firma.
"$BT/zipalign" -p -f 4 "$SIN_FIRMAR" "$SALIDA/alineado.apk"
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-key-alias "$KEYALIAS" \
    --ks-pass "pass:$KEYPASS" --key-pass "pass:$KEYPASS" \
    --out "$DESTINO" "$SALIDA/alineado.apk"
rm -f "$SALIDA/alineado.apk"

echo
echo "=== comprobaciones ==="
# Que es el paquete correcto, que NO es depurable y que lo firma nuestra clave.
# Sin esto no hay forma de notar que se ha publicado el APK equivocado.
"$BT/aapt" dump badging "$DESTINO" | grep -E "^package|targetSdkVersion|native-code|application-debuggable" || true
if "$BT/aapt" dump badging "$DESTINO" | grep -q application-debuggable; then
    echo "ERROR: el APK es depurable; no se publica"; exit 1
fi
"$BT/apksigner" verify --print-certs "$DESTINO" | grep -E "certificate DN|SHA-256 digest" | head -2
echo
echo "APK firmado: $DESTINO ($(stat -c%s "$DESTINO") bytes)"
