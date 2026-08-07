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

#ifndef ROUTESERVICE_H
#define ROUTESERVICE_H

#include "mapboxglengine.h"
#include "microhttpservicebase.h"
#include "valhallaengine.h"

/// Despachador de peticiones. Es el equivalente reducido de RequestMapper de
/// OSM Scout Server: mismos caminos y mismo formato de respuesta, pero solo con
/// lo que hace falta para rutas. Los demas motores (tiles, geocoder, POIs) se
/// iran anadiendo en las fases siguientes.
///
/// Los caminos salen de lo que pide Navius, no de la documentacion del servidor:
///   GET /v1/activate                    -> deteccion (NavSearch.js)
///   GET /v2/route?json=<consulta>       -> ruta
///   GET /v2/trace_attributes?json=<...> -> map matching
///   GET /v1/mbgl/style?style=osmbright  -> estilo del mapa
///   GET /v1/mbgl/tile?x=&y=&z=          -> tile vectorial
///   GET /v1/mbgl/glyphs?stack=&range=   -> fuentes
///   GET /v1/mbgl/sprite*.png|.json      -> iconos
class RouteService : public MicroHTTP::ServiceBase
{
public:
    RouteService(ValhallaEngine *valhalla, MapboxGLEngine *mapbox);

    unsigned int service(const char *url, MHD_Connection *connection,
                         MHD_Response *response,
                         MicroHTTP::Connection::keytype connection_id) override;

private:
    unsigned int serveMapboxGL(const QString &path, MHD_Connection *connection,
                               MHD_Response *response,
                               MicroHTTP::Connection::keytype connection_id);

    ValhallaEngine  *m_engine;
    MapboxGLEngine  *m_mapbox;
};

#endif // ROUTESERVICE_H
