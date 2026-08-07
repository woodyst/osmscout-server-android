/*
 * Copyright (C) 2016-2018 Rinigus https://github.com/rinigus
 * Copyright (C) 2026 EGP Sistemas
 *
 * Portado de server/src/mapboxglmaster.cpp de OSM Scout Server.
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

#include "mapboxglengine.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

// El marcador que llevan los .json de estilo y que se sustituye por el
// host:puerto del servidor (const_tag_hostname_port en el original).
const QString TAG_HOSTNAME_PORT = QStringLiteral("HOSTNAMEPORT");

/// Los estilos y sprites van dentro del APK, no en disco. El original los lee
/// del sistema de ficheros y se protege del path traversal comparando la ruta
/// canonica; aqui no hay ruta canonica que valga porque qrc es un sistema de
/// ficheros virtual de solo lectura, asi que se filtra el nombre a mano.
QString resourcePath(const QString &dname, const QString &fname)
{
    if (fname.contains(QLatin1Char('/')) || fname.contains(QLatin1String("..")))
        return QString();
    return QStringLiteral(":/styles/mapboxgl/") + dname + QLatin1Char('/') + fname;
}

bool readResource(const QString &path, QByteArray &result)
{
    if (path.isEmpty())
        return false;
    QFile fin(path);
    if (!fin.open(QIODevice::ReadOnly)) {
        qWarning() << "OSMSCOUT: no se pudo abrir" << path;
        return false;
    }
    result = fin.readAll();
    return !result.isEmpty();
}

} // namespace

MapboxGLEngine::MapboxGLEngine() = default;

bool MapboxGLEngine::running() const
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return !m_db_connection_fname.isEmpty();
}

int MapboxGLEngine::sectionCount() const
{
    std::unique_lock<std::mutex> lk(m_mutex);
    int n = 0;
    for (auto it = m_db_connection_fname.constBegin(); it != m_db_connection_fname.constEnd(); ++it)
        if (it.key() != const_conn_world && it.key() != const_conn_glyphs)
            ++n;
    return n;
}

void MapboxGLEngine::addConnection(const QString &connection, const QString &fname)
{
    m_db_connection[connection] = nullptr;
    m_db_connection_fname[connection] = fname;
}

bool MapboxGLEngine::start(const QString &mapsDir, const QString &hostnamePort)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    m_hostname_port = hostnamePort;
    m_db_connection.clear();
    m_db_connection_fname.clear();

    // Mismo reparto que deja el Map Manager: los .sqlite en packages/ y las
    // fuentes aparte en glyphs/, porque estas valen para todo el mundo.
    const QDir packages(mapsDir + QStringLiteral("/mapboxgl/packages"));
    const QString glyphs = mapsDir + QStringLiteral("/mapboxgl/glyphs/glyphs.sqlite");

    if (QFile::exists(glyphs))
        addConnection(const_conn_glyphs, glyphs);

    const QString world = packages.absoluteFilePath(QStringLiteral("tiles-world.sqlite"));
    if (QFile::exists(world))
        addConnection(const_conn_world, world);

    // Los nombres son tiles-section-7-71-38.sqlite; el original se queda con lo
    // que va tras "tiles-section-" (mid(14)) y lo usa como clave de conexion.
    const QStringList sections =
        packages.entryList(QStringList() << QStringLiteral("tiles-section-*.sqlite"), QDir::Files);
    for (const QString &s : sections) {
        const QFileInfo fi(packages.absoluteFilePath(s));
        addConnection(const_conn_prefix + fi.baseName().mid(14), fi.absoluteFilePath());
    }

    qInfo() << "OSMSCOUT: mapboxgl ->" << (m_db_connection_fname.contains(const_conn_world) ? "mundo si" : "mundo NO")
            << (m_db_connection_fname.contains(const_conn_glyphs) ? "glyphs si" : "glyphs NO")
            << "secciones:" << sections.size();

    return !m_db_connection_fname.isEmpty();
}

std::shared_ptr<sqlite3pp::database> MapboxGLEngine::getDatabase(const QString &connection)
{
    auto dbc = m_db_connection.find(connection);
    if (dbc == m_db_connection.end())
        return nullptr;
    if (dbc.value())
        return dbc.value();

    // Las conexiones se abren perezosamente: con los mapas de un pais son dos
    // docenas de ficheros y abrirlos todos al arrancar seria gastar descriptores
    // y memoria en bases que quiza no se toquen.
    auto nmc = m_db_connection_fname.find(connection);
    if (nmc == m_db_connection_fname.end())
        return nullptr;

    try {
        std::shared_ptr<sqlite3pp::database> db(
            new sqlite3pp::database(nmc.value().toStdString().c_str(), SQLITE_OPEN_READONLY));
        m_db_connection[connection] = db;
        return db;
    } catch (sqlite3pp::database_error &e) {
        qWarning() << "OSMSCOUT: no se pudo abrir la base de Mapbox GL:" << e.what();
    }

    return nullptr;
}

bool MapboxGLEngine::getTile(int x, int y, int z, QByteArray &result, bool &compressed,
                             bool &found)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    found = true;
    compressed = true; // los tiles vienen gzip dentro de la base

    // Por debajo del nivel de seccion todo esta en la base mundial; a partir de
    // ahi se localiza la seccion desplazando las coordenadas al zoom 7.
    QString connection;
    if (z < const_section_level) {
        connection = const_conn_world;
    } else {
        const int xx = x >> (z - const_section_level);
        const int yy = y >> (z - const_section_level);
        connection = const_conn_prefix + QStringLiteral("7-%1-%2").arg(xx).arg(yy);
    }

    std::shared_ptr<sqlite3pp::database> db = getDatabase(connection);
    if (!db) {
        found = false;
        return true;
    }

    try {
        sqlite3pp::query query(*db,
                               "SELECT tile_data FROM tiles WHERE "
                               "(zoom_level=:z AND tile_column=:x AND tile_row=:y)");
        query.bind(":x", x);
        // Las bases estan en TMS y la peticion viene en XYZ: la fila va invertida.
        query.bind(":y", (1 << z) - 1 - y);
        query.bind(":z", z);

        for (auto v : query) {
            void const *blob = v.get<void const *>(0);
            const int sz = v.column_bytes(0);
            result = QByteArray((const char *)blob, sz);
            return true;
        }
    } catch (sqlite3pp::database_error &e) {
        qWarning() << "OSMSCOUT: fallo la consulta de tiles:" << e.what();
        return false;
    }

    found = false;
    return true;
}

bool MapboxGLEngine::getGlyphs(const QString &stackstr, const QString &range,
                               QByteArray &result, bool &compressed, bool &found)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    found = true;
    compressed = false;

    std::shared_ptr<sqlite3pp::database> db = getDatabase(const_conn_glyphs);
    if (!db) {
        found = false;
        return true;
    }

    QStringList stacks = stackstr.split(QLatin1Char(','));

    // Igual que el original: Noto Sans Regular como respaldo de las fuentes por
    // defecto de MapboxGL y de la Noto Italic, a la que le faltan muchos
    // alfabetos.
    if ((stackstr.contains(QLatin1String("Open Sans Regular"))
         && stackstr.contains(QLatin1String("Arial Unicode MS Regular")))
        || stackstr.contains(QLatin1String("Noto Sans Italic")))
        stacks.append(QStringLiteral("Noto Sans Regular"));

    const std::string str_range = range.toStdString();
    for (const QString &stack : stacks) {
        try {
            const std::string str_stack = stack.toStdString();
            sqlite3pp::query query(*db, "SELECT pbf FROM fonts WHERE (stack=:stack AND range=:range)");
            query.bind(":stack", str_stack, sqlite3pp::nocopy);
            query.bind(":range", str_range, sqlite3pp::nocopy);

            for (auto v : query) {
                void const *blob = v.get<void const *>(0);
                const int sz = v.column_bytes(0);
                result = QByteArray((const char *)blob, sz);
                return true;
            }
        } catch (sqlite3pp::database_error &e) {
            qWarning() << "OSMSCOUT: fallo la consulta de fuentes:" << e.what();
            return false;
        }
    }

    found = false;
    return true;
}

bool MapboxGLEngine::getStyle(const QString &stylename, QByteArray &result)
{
    QByteArray raw;
    if (!readResource(resourcePath(QStringLiteral("styles"), stylename + QStringLiteral(".json")), raw))
        return false;

    // El estilo trae las URL de tiles, glyphs y sprites apuntando a HOSTNAMEPORT;
    // sin esta sustitucion el cliente las pediria a ningun sitio.
    QString style = QString::fromUtf8(raw);
    style.replace(TAG_HOSTNAME_PORT, m_hostname_port);
    result = style.toUtf8();
    return true;
}

bool MapboxGLEngine::getSpriteJson(const QString &fname, QByteArray &result)
{
    return readResource(resourcePath(QStringLiteral("sprites"), fname), result);
}

bool MapboxGLEngine::getSpriteImage(const QString &fname, QByteArray &result)
{
    return readResource(resourcePath(QStringLiteral("sprites"), fname), result);
}
