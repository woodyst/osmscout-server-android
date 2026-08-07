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

#include "routeservice.h"

#include "microhttpconnectionstore.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace {

void sendData(MicroHTTP::Connection::keytype id, MHD_Response *response,
              const QByteArray &payload, const char *contentType)
{
    QByteArray data = payload;
    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, contentType);
    MicroHTTP::ConnectionStore::setData(id, data, false);
}

unsigned int sendError(MicroHTTP::Connection::keytype id, MHD_Response *response,
                       const QString &text)
{
    qWarning() << "OSMSCOUT:" << text;
    QByteArray data = text.toUtf8();
    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE,
                            "text/plain; charset=UTF-8");
    MicroHTTP::ConnectionStore::setData(id, data, true);
    return MHD_HTTP_BAD_REQUEST;
}

} // namespace

RouteService::RouteService(ValhallaEngine *valhalla, MapboxGLEngine *mapbox, GeoEngine *geo,
                           const QString &mapsDir)
    : m_engine(valhalla), m_mapbox(mapbox), m_geo(geo), m_mapsDir(mapsDir)
{
}

unsigned int RouteService::serveStatus(MHD_Response *response,
                                       MicroHTTP::Connection::keytype connection_id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("routing"), m_engine->running());
    obj.insert(QStringLiteral("tiles"), m_mapbox->running());
    obj.insert(QStringLiteral("sections"), m_mapbox->sectionCount());
    obj.insert(QStringLiteral("search"), m_geo->running());
    obj.insert(QStringLiteral("territories"),
               QJsonArray::fromStringList(m_geo->territories()));
    obj.insert(QStringLiteral("mapsDir"), m_mapsDir);

    sendData(connection_id, response, QJsonDocument(obj).toJson(QJsonDocument::Compact),
             "application/json; charset=UTF-8");
    return MHD_HTTP_OK;
}

namespace {

/// Lee un argumento del query. Equivale al q2value<> del RequestMapper original.
QString arg(MHD_Connection *connection, const char *name, const QString &def = QString())
{
    const char *v = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, name);
    return (v && *v) ? QString::fromUtf8(v) : def;
}

void addLength(MHD_Response *response, const QByteArray &bytes)
{
    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH,
                            QByteArray::number(bytes.size()).constData());
}

} // namespace

unsigned int RouteService::serveMapboxGL(const QString &path, MHD_Connection *connection,
                                         MHD_Response *response,
                                         MicroHTTP::Connection::keytype connection_id)
{
    if (!m_mapbox->running())
        return sendError(connection_id, response,
                         QStringLiteral("No hay mapas vectoriales instalados"));

    // ── Estilo ──────────────────────────────────────────────────────────────
    if (path == QLatin1String("/v1/mbgl/style")) {
        const QString style = arg(connection, "style", QStringLiteral("osmbright"));
        QByteArray bytes;
        if (!m_mapbox->getStyle(style, bytes))
            return sendError(connection_id, response,
                             QStringLiteral("No existe el estilo ") + style);

        MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
        MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
        addLength(response, bytes);
        return MHD_HTTP_OK;
    }

    // ── Tile ────────────────────────────────────────────────────────────────
    if (path == QLatin1String("/v1/mbgl/tile")) {
        bool okx = false, oky = false, okz = false;
        const int x = arg(connection, "x").toInt(&okx);
        const int y = arg(connection, "y").toInt(&oky);
        const int z = arg(connection, "z").toInt(&okz);
        if (!okx || !oky || !okz || x < 0 || y < 0 || z < 0)
            return sendError(connection_id, response,
                             QStringLiteral("Peticion de tile mal formada"));

        bool compressed = false, found = true;
        QByteArray bytes;
        if (!m_mapbox->getTile(x, y, z, bytes, compressed, found))
            return sendError(connection_id, response, QStringLiteral("Fallo al leer el tile"));

        if (!found) {
            // 418 a proposito, no 404, y esto viene del servidor original: Mapbox
            // GL Native interpreta el 404 como "tile vacio" y pinta un hueco,
            // mientras que con otro codigo se sube al tile padre y amplia. Ver
            // mapbox/mapbox-gl-native#10545.
            QByteArray empty;
            MicroHTTP::ConnectionStore::setData(connection_id, empty, false);
            return 418;
        }

        MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
        MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/x-protobuf");
        if (compressed)
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
        addLength(response, bytes);
        return MHD_HTTP_OK;
    }

    // ── Fuentes ─────────────────────────────────────────────────────────────
    if (path == QLatin1String("/v1/mbgl/glyphs")) {
        const QString stack = arg(connection, "stack");
        const QString range = arg(connection, "range");
        if (stack.isEmpty() || range.isEmpty())
            return sendError(connection_id, response,
                             QStringLiteral("Peticion de fuentes mal formada"));

        bool compressed = false, found = true;
        QByteArray bytes;
        if (!m_mapbox->getGlyphs(stack, range, bytes, compressed, found))
            return sendError(connection_id, response, QStringLiteral("Fallo al leer las fuentes"));
        if (!found)
            return MHD_HTTP_NOT_FOUND;

        MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
        MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/x-protobuf");
        if (compressed)
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
        addLength(response, bytes);
        return MHD_HTTP_OK;
    }

    // ── Iconos ──────────────────────────────────────────────────────────────
    if (path.startsWith(QLatin1String("/v1/mbgl/sprite"))) {
        QString fname = path.mid(9); // longitud de "/v1/mbgl/"
        // El cliente pide la version @2x para pantallas densas; solo hay un
        // juego de sprites, asi que se sirve el mismo, igual que el original.
        fname.replace(QLatin1String("@2x"), QLatin1String(""));

        QByteArray bytes;
        if (fname.endsWith(QLatin1String(".png"))) {
            if (!m_mapbox->getSpriteImage(fname, bytes))
                return MHD_HTTP_NOT_FOUND;
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "image/png");
        } else if (fname.endsWith(QLatin1String(".json"))) {
            if (!m_mapbox->getSpriteJson(fname, bytes))
                return MHD_HTTP_NOT_FOUND;
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
        } else {
            return sendError(connection_id, response,
                             QStringLiteral("Peticion de sprite mal formada: ") + fname);
        }

        MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
        MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
        addLength(response, bytes);
        return MHD_HTTP_OK;
    }

    return sendError(connection_id, response, QStringLiteral("Unknown URL path: ") + path);
}

unsigned int RouteService::service(const char *url, MHD_Connection *connection,
                                   MHD_Response *response,
                                   MicroHTTP::Connection::keytype connection_id)
{
    const QString path = QString::fromUtf8(url);

    // Se registra cada peticion. Es la unica forma de ver desde fuera que Navius
    // esta hablando con nosotros y no con el servidor online: los dos devuelven
    // el mismo JSON y por la pantalla no se distingue.
    qInfo() << "OSMSCOUT: peticion" << path;

    // Deteccion. Navius lo pide al arrancar (NavSearch.js:65) y espera hasta 30 s
    // porque en Ubuntu Touch es D-Bus quien arranca el servidor; aqui ya estamos
    // en pie, asi que responde al primer intento, el rapido de 2 s.
    if (path == QLatin1String("/v1/activate")) {
        sendData(connection_id, response, QByteArrayLiteral("{ \"status\": \"active\" }"),
                 "application/json; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    // Estado para la interfaz del propio servidor, que desde que el servidor
    // vive en su propio proceso ya no tiene los motores a mano. No es del
    // contrato del original: es nuestro.
    if (path == QLatin1String("/v1/status"))
        return serveStatus(response, connection_id);

    if (path.startsWith(QLatin1String("/v1/mbgl")))
        return serveMapboxGL(path, connection, response, connection_id);

    if (path == QLatin1String("/v1/search") || path == QLatin1String("/v2/search")
        || path == QLatin1String("/v1/guide") || path == QLatin1String("/v1/poi_types"))
        return serveSearch(path, connection, response, connection_id);

    ValhallaEngine::ActorType actor;
    if (path == QLatin1String("/v2/route"))
        actor = ValhallaEngine::Route;
    else if (path == QLatin1String("/v2/trace_attributes"))
        actor = ValhallaEngine::TraceAttributes;
    else if (path == QLatin1String("/v2/trace_route"))
        actor = ValhallaEngine::TraceRoute;
    else if (path == QLatin1String("/v2/locate"))
        actor = ValhallaEngine::Locate;
    else if (path == QLatin1String("/v2/matrix"))
        actor = ValhallaEngine::Matrix;
    else if (path == QLatin1String("/v2/optimized_route"))
        actor = ValhallaEngine::OptimizedRoute;
    else if (path == QLatin1String("/v2/isochrone"))
        actor = ValhallaEngine::Isochrone;
    else if (path == QLatin1String("/v2/height"))
        actor = ValhallaEngine::Height;
    else
        return sendError(connection_id, response,
                         QStringLiteral("Unknown URL path: ") + path);

    // Navius manda la consulta en el query, no en el cuerpo:
    //   GET /v2/route?json=<encodeURIComponent(body)>
    // Al ir codificada con encodeURIComponent, los & y = de dentro del JSON
    // vienen escapados y MHD no parte el argumento.
    const char *json = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "json");
    if (json == nullptr || *json == '\0')
        return sendError(connection_id, response,
                         QStringLiteral("Error while reading Valhalla's query"));

    if (!m_engine->running())
        return sendError(connection_id, response,
                         QStringLiteral("Valhalla no esta arrancado"));

    QByteArray result;
    QString error;
    // Sincrono a proposito. OSM Scout Server lo hace en un QThreadPool, pero
    // actor_t va serializado por su mutex de todos modos, y libmicrohttpd ya
    // atiende cada peticion en su propio hilo.
    if (!m_engine->callActor(actor, QByteArray(json), result, error))
        return sendError(connection_id, response,
                         QStringLiteral("Error en Valhalla: ") + error);

    sendData(connection_id, response, result, "application/json; charset=UTF-8");
    return MHD_HTTP_OK;
}

unsigned int RouteService::serveSearch(const QString &path, MHD_Connection *connection,
                                       MHD_Response *response,
                                       MicroHTTP::Connection::keytype connection_id)
{
    if (!m_geo->running())
        return sendError(connection_id, response,
                         QStringLiteral("No hay datos de busqueda instalados"));

    // ── Tipos de POI ────────────────────────────────────────────────────────
    if (path == QLatin1String("/v1/poi_types")) {
        QByteArray types;
        if (!m_geo->poiTypes(types))
            return sendError(connection_id, response,
                             QStringLiteral("No se pudieron leer los tipos de POI"));
        sendData(connection_id, response, types, "application/json; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    // ── POIs cercanos ───────────────────────────────────────────────────────
    if (path == QLatin1String("/v1/guide")) {
        // poitype o query, que el original acepta los dos por compatibilidad.
        QString poitype = arg(connection, "poitype");
        if (poitype.isEmpty()) poitype = arg(connection, "query");
        const QString name = arg(connection, "name");
        if (poitype.isEmpty() && name.isEmpty())
            return sendError(connection_id, response,
                             QStringLiteral("Error while reading guide query parameters"));

        QByteArray pois;
        if (!m_geo->guide(poitype, name,
                          arg(connection, "lat").toDouble(),
                          arg(connection, "lng").toDouble(),
                          arg(connection, "radius", QStringLiteral("1000")).toDouble(),
                          arg(connection, "limit", QStringLiteral("50")).toUInt(),
                          pois))
            return sendError(connection_id, response, QStringLiteral("Fallo la busqueda de POIs"));

        sendData(connection_id, response, pois, "application/json; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    const QString pattern = arg(connection, "search").simplified();
    if (pattern.isEmpty())
        return sendError(connection_id, response,
                         QStringLiteral("Error while reading search query parameters"));

    const size_t limit = arg(connection, "limit", QStringLiteral("25")).toUInt();

    // El punto de referencia sesga los resultados hacia donde esta el usuario.
    // Solo se aplica si vienen las dos coordenadas, igual que en el original.
    //
    // El zoom por defecto NO es el 16 del original: manda el radio del sesgo,
    // (1 << (18 - zoom)) * 250 m, y 16 son mil metros. Conduciendo se busca a
    // escala de comarca, asi que 12 —16 km— es lo razonable. El cliente puede
    // pasar el suyo. Ver vendor/geocoder-nlp/CAMBIOS.md: con el original el
    // radio se quedaba en 250 m hiciera lo que hiciera el cliente.
    GeoNLP::Geocoder::GeoReference reference;
    const QString lat = arg(connection, "lat");
    const QString lng = arg(connection, "lng");
    if (!lat.isEmpty() && !lng.isEmpty())
        reference.set(lat.toDouble(), lng.toDouble(),
                      arg(connection, "zoom", QStringLiteral("12")).toUInt(),
                      arg(connection, "importance", QStringLiteral("0.75")).toDouble());

    QByteArray result;
    if (!m_geo->search(pattern, limit, reference,
                       path == QLatin1String("/v2/search"), result))
        return sendError(connection_id, response, QStringLiteral("Fallo la busqueda"));

    sendData(connection_id, response, result, "application/json; charset=UTF-8");
    return MHD_HTTP_OK;
}
