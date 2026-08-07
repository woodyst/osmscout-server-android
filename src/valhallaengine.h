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

#ifndef VALHALLAENGINE_H
#define VALHALLAENGINE_H

#include <QByteArray>
#include <QString>

#include <memory>
#include <mutex>

namespace valhalla {
namespace tyr {
class actor_t;
}
} // namespace valhalla

/// Envuelve el actor_t de Valhalla. Es la traduccion a Android de
/// ValhallaMaster de OSM Scout Server, quitandole lo que aqui no existe:
/// D-Bus, el proceso separado y los ajustes por QSettings.
///
/// El mutex no es opcional: actor_t NO es reentrante, y libmicrohttpd atiende
/// cada peticion en su propio hilo.
class ValhallaEngine
{
public:
    enum ActorType {
        Route,
        TraceAttributes,
        TraceRoute,
        Locate,
        Matrix,
        OptimizedRoute,
        Isochrone,
        Height,
    };

    ValhallaEngine();
    ~ValhallaEngine();

    /// Arranca el motor con los tiles del directorio dado. Devuelve false si la
    /// configuracion no es valida o no hay tiles.
    bool start(const QString &tileDir);
    void stop();
    bool running() const;

    QString tileDir() const { return m_tile_dir; }

    /// Ejecuta una consulta. `json` es el cuerpo tal cual lo manda Navius.
    bool callActor(ActorType atype, const QByteArray &json, QByteArray &result,
                   QString &error);

private:
    std::unique_ptr<valhalla::tyr::actor_t> m_actor;
    mutable std::mutex m_mutex;
    QString m_tile_dir;
};

#endif // VALHALLAENGINE_H
