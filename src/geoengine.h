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

#include <mutex>

#include "geocoder.h"
#include "postal.h"

/// Busqueda de destinos. Traduccion de GeoMaster de OSM Scout Server,
/// conservando su cadena: libpostal trocea y normaliza la consulta, y
/// geocoder-nlp busca sobre el trie de marisa y la base de kyotocabinet.
///
/// Diferencia con el original: aqui se carga UNA base de geocoder a la vez, la
/// del territorio elegido. El original itera sobre varias segun el mapa
/// seleccionado en sus ajustes, y esa seleccion aun no existe en este port.
class GeoEngine
{
public:
    GeoEngine();

    /// Carga la base de <mapsDir>/geocoder-nlp/<territorio> y los datos de
    /// libpostal. Si no se indica territorio, coge el primero que encuentre.
    bool start(const QString &mapsDir, const QString &language = QStringLiteral("es"),
               const QString &territory = QString());
    bool running() const;

    QString territory() const { return m_territory; }

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
    mutable std::mutex m_mutex;
    GeoNLP::Geocoder m_geocoder;
    GeoNLP::Postal   m_postal;
    QString m_territory;
    bool m_loaded{false};
};

#endif // GEOENGINE_H
