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

#include "valhallaengine.h"

#include <QDebug>
#include <QDir>
#include <QFile>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <valhalla/tyr/actor.h>

#include <sstream>

namespace {

// Los mismos marcadores que sustituye ValhallaMaster de OSM Scout Server; los
// nombres salen de su valhallamaster.h. La plantilla va por version de Valhalla:
// data/valhalla.json-3.4.0.
void replaceTag(QString &conf, const QString &tag, const QString &value)
{
    conf.replace(tag, value);
}

} // namespace

ValhallaEngine::ValhallaEngine() = default;
ValhallaEngine::~ValhallaEngine() = default;

bool ValhallaEngine::running() const
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_actor != nullptr;
}

void ValhallaEngine::stop()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    m_actor.reset();
}

bool ValhallaEngine::start(const QString &tileDir)
{
    if (!QDir(tileDir).exists()) {
        qWarning() << "OSMSCOUT: no existe el directorio de tiles" << tileDir;
        return false;
    }

    QFile fin(QStringLiteral(":/data/valhalla.json-3.4.0"));
    if (!fin.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "OSMSCOUT: no se pudo leer la plantilla de configuracion";
        return false;
    }
    QString conf = QString::fromUtf8(fin.readAll());

    replaceTag(conf, QStringLiteral("VALHALLA_TILE_DIRECTORY"), tileDir);
    replaceTag(conf, QStringLiteral("MAXIMAL_CACHE_SIZE"),
               QString::number(64L * 1024 * 1024));
    replaceTag(conf, QStringLiteral("LIMIT_MAX_DISTANCE_AUTO"), QStringLiteral("5000000"));
    replaceTag(conf, QStringLiteral("LIMIT_MAX_DISTANCE_BICYCLE"), QStringLiteral("500000"));
    replaceTag(conf, QStringLiteral("LIMIT_MAX_DISTANCE_PEDESTRIAN"), QStringLiteral("250000"));

    boost::property_tree::ptree pt;
    try {
        std::stringstream ss(conf.toStdString());
        boost::property_tree::read_json(ss, pt);
    } catch (const std::exception &e) {
        qWarning() << "OSMSCOUT: configuracion de Valhalla invalida:" << e.what();
        return false;
    }

    // tile_extract apunta a un .tar que no existe: los paquetes que descarga el
    // Map Manager dejan los tiles sueltos. Si se deja puesto, Valhalla intenta
    // el tar primero y avisa por cada arranque.
    try {
        pt.get_child("mjolnir").erase("tile_extract");
    } catch (const std::exception &) {
        // si no estaba, mejor
    }

    std::unique_lock<std::mutex> lk(m_mutex);
    try {
        m_actor.reset(new valhalla::tyr::actor_t(pt, true)); // con autoclean
    } catch (const std::exception &e) {
        qWarning() << "OSMSCOUT: no se pudo arrancar Valhalla:" << e.what();
        m_actor.reset();
        return false;
    }

    m_tile_dir = tileDir;
    qInfo() << "OSMSCOUT: Valhalla arrancado con tiles en" << tileDir;
    return true;
}

bool ValhallaEngine::callActor(ActorType atype, const QByteArray &json,
                               QByteArray &result, QString &error)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (!m_actor) {
        error = QStringLiteral("Valhalla no esta arrancado");
        return false;
    }

    try {
        const std::string s = json.toStdString();
        std::string r;
        switch (atype) {
        case Route:           r = m_actor->route(s); break;
        case TraceAttributes: r = m_actor->trace_attributes(s); break;
        case TraceRoute:      r = m_actor->trace_route(s); break;
        case Locate:          r = m_actor->locate(s); break;
        case Matrix:          r = m_actor->matrix(s); break;
        case OptimizedRoute:  r = m_actor->optimized_route(s); break;
        case Isochrone:       r = m_actor->isochrone(s); break;
        case Height:          r = m_actor->height(s); break;
        }
        result = QByteArray::fromStdString(r);
        return true;
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
        // Igual que OSM Scout Server: tras una excepcion el actor puede quedar
        // con estado a medias, asi que se limpia antes de la siguiente consulta.
        m_actor->cleanup();
        return false;
    }
}
