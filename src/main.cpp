/*
 * Copyright (C) 2026 EGP Sistemas
 *
 * This file is part of OSM Scout Server for Android.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

#include "cerrtolog.h"
#include "geoengine.h"
#include "mapboxglengine.h"
#include "microhttpserver.h"
#include "routeservice.h"
#include "valhallaengine.h"

namespace {

const quint16 SERVER_PORT = 8553;

/// Busca los mapas. El orden no es caprichoso:
///
///  1. El directorio externo propio de la app. Es una ruta REAL del sistema de
///     ficheros, accesible por USB y sin pedir permisos, y en un movil con
///     microSD getExternalFilesDirs() devuelve tambien la tarjeta. Hace falta
///     que sea real porque Valhalla mapea los tiles en memoria: un content://
///     del SAF no sirve, cosa ya aprendida portando la musica de Navius.
///  2. /data/local/tmp, que es por donde entran los datos en las pruebas por adb.
QString findMapsDir()
{
    QStringList candidates;

    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)) {
        if (dir.contains(QStringLiteral("/Android/data/")))
            candidates << dir + QStringLiteral("/Maps.OSM");
    }
    candidates << QStringLiteral("/data/local/tmp");

    // Vale con que haya UNO de los dos motores: se puede tener rutas sin tiles
    // o al reves mientras se van instalando mapas.
    for (const QString &c : candidates) {
        if (QDir(c + QStringLiteral("/valhalla/tiles")).exists()
            || QDir(c + QStringLiteral("/mapboxgl")).exists()) {
            qInfo() << "OSMSCOUT: mapas encontrados en" << c;
            return c;
        }
    }

    qWarning() << "OSMSCOUT: no se encontraron mapas. Buscado en:" << candidates;
    return QString();
}

#ifdef Q_OS_ANDROID
/// Levanta el servicio en primer plano (android/src/.../ServerService.java).
///
/// El servicio no sirve nada: existe solo para que Android no mate ESTE proceso,
/// que es donde viven el servidor HTTP y Valhalla, cuando el usuario se pase a
/// Navius. Ver el comentario de la clase Java.
void startForegroundService()
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        qWarning() << "OSMSCOUT: sin contexto de Android, no se arranca el servicio";
        return;
    }

    QJniObject::callStaticMethod<void>("com/egpsistemas/osmscout/ServerService",
                                       "start", "(Landroid/content/Context;)V",
                                       context.object());
    // Cualquier excepcion pendiente de JNI hay que limpiarla o la siguiente
    // llamada al VM aborta el proceso. Ya paso en el port de Navius con el SAF.
    if (QJniEnvironment().checkAndClearExceptions())
        qWarning() << "OSMSCOUT: fallo al arrancar el servicio en primer plano";
    else
        qInfo() << "OSMSCOUT: servicio en primer plano arrancado";
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    // Lo primero: sin esto los errores del codigo vendorizado, que van a
    // std::cerr, se pierden sin dejar rastro en Android.
    static CerrToLog cerrToLog;

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OSM Scout Server"));
    app.setOrganizationName(QStringLiteral("EGP Sistemas"));

    const QString mapsDir = findMapsDir();

    static ValhallaEngine engine;
    const bool engineOk = !mapsDir.isEmpty()
                          && engine.start(mapsDir + QStringLiteral("/valhalla/tiles"));

    static MapboxGLEngine mapbox;
    // El host:puerto se lo queda el motor para sustituirlo dentro del JSON de
    // estilo, que trae las URL de tiles, fuentes e iconos apuntando a un
    // marcador.
    const bool mapboxOk = !mapsDir.isEmpty()
                          && mapbox.start(mapsDir,
                                          QStringLiteral("127.0.0.1:%1").arg(SERVER_PORT));

    static GeoEngine geo;
    const bool geoOk = !mapsDir.isEmpty() && geo.start(mapsDir);

    static RouteService service(&engine, &mapbox, &geo);

    // Solo loopback. El servidor no tiene autenticacion ninguna —igual que en
    // Ubuntu Touch— asi que no puede quedar expuesto a la red: quien tiene que
    // alcanzarlo es otra app del mismo dispositivo.
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    static MicroHTTP::Server server(&service, address, -1);
    const bool serverOk = static_cast<bool>(server);

    qInfo() << "OSMSCOUT: rutas" << (engineOk ? "OK" : "KO")
            << "tiles" << (mapboxOk ? "OK" : "KO")
            << "busqueda" << (geoOk ? "OK" : "KO")
            << "servidor" << (serverOk ? "OK" : "KO")
            << "puerto" << SERVER_PORT;

#ifdef Q_OS_ANDROID
    if (serverOk)
        startForegroundService();
#endif

    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty(QStringLiteral("engineOk"), engineOk);
    qml.rootContext()->setContextProperty(QStringLiteral("mapboxOk"), mapboxOk);
    qml.rootContext()->setContextProperty(QStringLiteral("geoOk"), geoOk);
    qml.rootContext()->setContextProperty(QStringLiteral("territory"), geo.territory());
    qml.rootContext()->setContextProperty(QStringLiteral("sectionCount"), mapbox.sectionCount());
    qml.rootContext()->setContextProperty(QStringLiteral("serverOk"), serverOk);
    qml.rootContext()->setContextProperty(QStringLiteral("tileDir"),
                                          mapsDir.isEmpty() ? QStringLiteral("—") : mapsDir);
    qml.rootContext()->setContextProperty(QStringLiteral("serverPort"), int(SERVER_PORT));
    qml.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (qml.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
