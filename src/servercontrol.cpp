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

#include "servercontrol.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>   // QNativeInterface::QAndroidApplication
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace {

#ifdef Q_OS_ANDROID
/// Llama a un metodo estatico de ServerService que solo recibe el contexto.
///
/// Cualquier excepcion pendiente de JNI hay que limpiarla o la siguiente llamada
/// al VM aborta el proceso. Ya paso en el port de Navius con el SAF.
void callService(const char *method)
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        qWarning() << "OSMSCOUT: sin contexto de Android, no se toca el servicio";
        return;
    }

    QJniObject::callStaticMethod<void>("com/egpsistemas/osmscout/ServerService",
                                       method, "(Landroid/content/Context;)V",
                                       context.object());
    if (QJniEnvironment().checkAndClearExceptions())
        qWarning() << "OSMSCOUT: fallo al llamar a ServerService." << method;
    else
        qInfo() << "OSMSCOUT: ServerService." << method;
}
#endif

} // namespace

ServerControl::ServerControl(quint16 port, QObject *parent)
    : QObject(parent)
    , m_statusUrl(QStringLiteral("http://127.0.0.1:%1/v1/status").arg(port))
{
    // Dos segundos: lo que se mira aqui es si el servidor esta en pie y que
    // mapas ha cargado, no algo que cambie deprisa. Arrancar los motores de un
    // territorio grande lleva un rato, asi que hay que seguir preguntando.
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &ServerControl::poll);
    m_timer.start();
    poll();
}

void ServerControl::start()
{
    m_starting = true;
    emit changed();
#ifdef Q_OS_ANDROID
    callService("start");
#endif
}

void ServerControl::stop()
{
    m_starting = false;
#ifdef Q_OS_ANDROID
    callService("stop");
#endif
    // Sin esperar a la siguiente consulta: al usuario le acaba de desaparecer
    // el servidor y la pantalla tiene que decirlo ya.
    m_running = m_routing = m_tiles = m_search = false;
    m_sections = 0;
    m_territories.clear();
    emit changed();
}

void ServerControl::restart()
{
    stop();
    // Un respiro para que Android termine de matar el proceso y suelte el
    // puerto: si se vuelve a arrancar en el acto, el nuevo no puede escuchar.
    QTimer::singleShot(1500, this, [this]() { start(); });
}

void ServerControl::poll()
{
    if (m_inFlight)
        return;   // el servidor esta arrancando y tarda; no encolar peticiones

    QNetworkRequest req{QUrl(m_statusUrl)};
    req.setTransferTimeout(2000);
    QNetworkReply *reply = m_net.get(req);
    m_inFlight = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_inFlight = false;
        reply->deleteLater();

        const bool antes = m_running;
        const QJsonObject o = (reply->error() == QNetworkReply::NoError)
                                  ? QJsonDocument::fromJson(reply->readAll()).object()
                                  : QJsonObject();

        m_running = !o.isEmpty();
        m_routing = o.value(QStringLiteral("routing")).toBool();
        m_tiles   = o.value(QStringLiteral("tiles")).toBool();
        m_search  = o.value(QStringLiteral("search")).toBool();
        m_sections = o.value(QStringLiteral("sections")).toInt();

        m_territories.clear();
        for (const QJsonValue &v : o.value(QStringLiteral("territories")).toArray())
            m_territories << v.toString();

        if (!antes && m_running) {
            m_starting = false;
            qInfo() << "OSMSCOUT: el servidor responde";
        }

        emit changed();
    });
}
