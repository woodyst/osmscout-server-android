/*
 * Copyright (C) 2016-2018 Rinigus https://github.com/rinigus
 * Copyright (C) 2026 EGP Sistemas
 *
 * Portado de server/src/mapboxglmaster.{h,cpp} de OSM Scout Server.
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

#ifndef MAPBOXGLENGINE_H
#define MAPBOXGLENGINE_H

#include <QByteArray>
#include <QHash>
#include <QString>

#include <memory>
#include <mutex>

#include <sqlite3pp.h>

/// Tiles vectoriales. Traduccion de MapboxGLMaster de OSM Scout Server, con el
/// mismo algoritmo de busqueda: por debajo de zoom 7 la base mundial, y de 7 en
/// adelante la seccion que sale de desplazar x e y.
///
/// Se le quita lo que aqui no aplica: el singleton, las senales de Qt y los
/// ajustes por QSettings. Los ficheros los localiza el propio motor a partir del
/// directorio de mapas, en vez de recibirlos del Map Manager.
class MapboxGLEngine
{
public:
    MapboxGLEngine();

    /// Busca las bases bajo <mapsDir>/mapboxgl: packages/tiles-world.sqlite,
    /// packages/tiles-section-*.sqlite y glyphs/glyphs.sqlite. Devuelve false si
    /// no hay ninguna.
    bool start(const QString &mapsDir, const QString &hostnamePort);
    bool running() const;

    int sectionCount() const;

    bool getTile(int x, int y, int z, QByteArray &result, bool &compressed, bool &found);
    bool getGlyphs(const QString &stack, const QString &range, QByteArray &result,
                   bool &compressed, bool &found);
    bool getStyle(const QString &stylename, QByteArray &result);
    bool getSpriteJson(const QString &fname, QByteArray &result);
    bool getSpriteImage(const QString &fname, QByteArray &result);

private:
    void addConnection(const QString &connection, const QString &fname);
    std::shared_ptr<sqlite3pp::database> getDatabase(const QString &connection);

    mutable std::mutex m_mutex;
    QHash<QString, std::shared_ptr<sqlite3pp::database>> m_db_connection;
    QHash<QString, QString> m_db_connection_fname;
    QString m_hostname_port;

    // Iguales que en el original (mapboxglmaster.h): el nivel de seccion es 7 y
    // los nombres de conexion se construyen con estos prefijos.
    const int     const_section_level{7};
    const QString const_conn_world{QStringLiteral("mapboxgl: world")};
    const QString const_conn_glyphs{QStringLiteral("mapboxgl: glyphs")};
    const QString const_conn_prefix{QStringLiteral("mapboxgl: ")};
};

#endif // MAPBOXGLENGINE_H
