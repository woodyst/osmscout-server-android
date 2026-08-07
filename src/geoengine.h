/*
 * Copyright (C) 2016-2018 Rinigus https://github.com/rinigus
 * Copyright (C) 2026 EGP Sistemas
 *
 * Portado de server/src/geomaster.{h,cpp} de OSM Scout Server.
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

#ifndef GEOENGINE_H
#define GEOENGINE_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <mutex>

#include "geocoder.h"
#include "postal.h"

/// Busqueda de destinos y de POIs. Traduccion de GeoMaster de OSM Scout Server,
/// conservando su cadena: libpostal trocea y normaliza la consulta, y
/// geocoder-nlp busca sobre el trie de marisa y la base de kyotocabinet.
///
/// geocoder-nlp solo puede tener UNA base abierta, asi que buscar en varios
/// territorios significa ir abriendolos por turnos dentro de la consulta. Es
/// exactamente lo que hace el original en GeoMaster::search(), y por eso el
/// coste crece con el numero de territorios instalados.
class GeoEngine
{
public:
    GeoEngine();

    /// Lee los territorios instalados de countries_requested.json, carga los
    /// datos de libpostal y deja abierto el mayor. Los demas se abren cuando
    /// hace falta.
    bool start(const QString &mapsDir, const QString &language = QStringLiteral("es"));
    bool running() const;

    /// Todos los territorios en los que se busca, del mayor al menor.
    QStringList territories() const;

    /// Devuelve un array JSON de {title, admin_region, lat, lng, type}, que es
    /// el mismo contrato que /v1/search del servidor original.
    bool search(const QString &pattern, size_t limit,
                const GeoNLP::Geocoder::GeoReference &reference,
                bool fullResult, QByteArray &result);

    /// POIs cerca de un punto. Devuelve lo mismo que search() mas "distance".
    /// Es /v1/guide del servidor original, y va por el MISMO motor: su guide()
    /// usa search_nearby() de geocoder-nlp, no libosmscout.
    bool guide(const QString &poitype, const QString &name, double lat, double lon,
               double radius, size_t limit, QByteArray &result);

    /// Tipos de POI conocidos, sacados de la tabla de alias. En el original esto
    /// lo da libosmscout, que aqui no hace falta portar solo por una lista.
    bool poiTypes(QByteArray &result);

private:
    struct Territory {
        QString id;         ///< europe-spain: el directorio dentro de geocoder-nlp
        QString geoPath;    ///< ruta absoluta de la base del geocoder
        QString postalDir;  ///< datos de pais de libpostal; vacio si no hay
        qint64  size{0};    ///< para ordenar de mayor a menor
    };

    /// Deja abierta la base de t. Si ya lo estaba no hace nada, que abrir una
    /// base no es gratis y lo normal es repetir territorio entre consultas.
    bool ensureOpen(const Territory &t);

    /// Los territorios que declara countries_requested.json. Si el fichero no
    /// esta —instalaciones anteriores a que se escribiera— se recurre a mirar
    /// que directorios hay en geocoder-nlp/, sin datos de pais de libpostal.
    QVector<Territory> readInstalled(const QString &mapsDir) const;

    mutable std::mutex m_mutex;
    GeoNLP::Geocoder m_geocoder;
    GeoNLP::Postal   m_postal;
    QVector<Territory> m_list;
    QString m_open;          ///< id del territorio abierto ahora mismo
    bool m_loaded{false};
};

#endif // GEOENGINE_H
