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

#ifndef SERVERCONTROL_H
#define SERVERCONTROL_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>

/// El puente entre la interfaz y el servidor, que desde que este vive en su
/// propio proceso ya no se puede consultar llamando a los motores.
///
/// Hace dos cosas: arrancar y parar el servicio de Android, y preguntarle el
/// estado por HTTP. Es exactamente lo mismo que hace Navius, solo que ademas
/// puede apagarlo.
class ServerControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool starting READ starting NOTIFY changed)
    Q_PROPERTY(bool routing READ routing NOTIFY changed)
    Q_PROPERTY(bool tiles READ tiles NOTIFY changed)
    Q_PROPERTY(bool search READ search NOTIFY changed)
    Q_PROPERTY(int sections READ sections NOTIFY changed)
    Q_PROPERTY(QString territories READ territories NOTIFY changed)

public:
    explicit ServerControl(quint16 port, QObject *parent = nullptr);

    bool running() const { return m_running; }
    /// Se le ha pedido que arranque y todavia no responde. No es un detalle
    /// cosmetico: cargar los motores de un territorio grande pasa de veinte
    /// segundos y, sin esto, la pantalla dice «No disponible» todo ese rato.
    bool starting() const { return m_starting && !m_running; }
    bool routing() const { return m_routing; }
    bool tiles() const   { return m_tiles; }
    bool search() const  { return m_search; }
    int  sections() const { return m_sections; }
    QString territories() const { return m_territories.join(QStringLiteral(", ")); }

    /// Levanta el servicio. Si ya esta corriendo, Android no lo duplica: le
    /// llega otro onStartCommand y ya.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    /// Parar y volver a arrancar. Es lo que hay que hacer tras descargar mapas:
    /// los motores mapean ficheros en memoria y no se recargan en caliente.
    Q_INVOKABLE void restart();

signals:
    void changed();

private:
    void poll();

    QNetworkAccessManager m_net;
    QTimer m_timer;
    QString m_statusUrl;
    QStringList m_territories;
    bool m_starting{false};
    bool m_running{false};
    bool m_routing{false};
    bool m_tiles{false};
    bool m_search{false};
    int  m_sections{0};
    bool m_inFlight{false};
};

#endif // SERVERCONTROL_H
