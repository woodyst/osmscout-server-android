/*
 * Copyright (C) 2016-2018 Rinigus https://github.com/rinigus
 * Copyright (C) 2026 EGP Sistemas
 *
 * Portado de server/src/geomaster.cpp de OSM Scout Server.
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

#include "geoengine.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {

/// Los extractos se solapan en las fronteras: Andorra la Vella sale igual en
/// europe-spain que en europe-andorra, con las mismas coordenadas. Buscando en
/// un solo territorio eso no pasaba nunca; buscando en todos, es lo normal.
/// Se quita el repetido conservando el primero, que tras ordenar es el mejor.
void dropDuplicates(std::vector<GeoNLP::Geocoder::GeoResult> &v)
{
    QSet<QString> seen;
    auto last = std::remove_if(v.begin(), v.end(),
        [&seen](const GeoNLP::Geocoder::GeoResult &r) {
            const QString key = QString::number(r.latitude, 'f', 6)
                              + QLatin1Char(',') + QString::number(r.longitude, 'f', 6)
                              + QLatin1Char('|') + QString::fromStdString(r.title);
            if (seen.contains(key))
                return true;
            seen.insert(key);
            return false;
        });
    v.erase(last, v.end());
}

} // namespace

GeoEngine::GeoEngine() = default;

bool GeoEngine::running() const
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_loaded;
}

QStringList GeoEngine::territories() const
{
    std::unique_lock<std::mutex> lk(m_mutex);
    QStringList out;
    for (const Territory &t : m_list)
        out << t.id;
    return out;
}

QVector<GeoEngine::Territory> GeoEngine::readInstalled(const QString &mapsDir) const
{
    QVector<Territory> out;
    const QDir geoRoot(mapsDir + QStringLiteral("/geocoder-nlp"));

    // El Map Manager guarda, por territorio, la entrada entera del catalogo: de
    // ahi salen tanto la ruta de la base como la de los datos de pais de
    // libpostal, que antes se adivinaban con una tabla de cuatro paises.
    QFile f(mapsDir + QStringLiteral("/countries_requested.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject inst = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = inst.constBegin(); it != inst.constEnd(); ++it) {
            const QJsonObject entry = it.value().toObject();
            const QString geoRel =
                entry.value(QStringLiteral("geocoder_nlp")).toObject()
                     .value(QStringLiteral("path")).toString();
            if (geoRel.isEmpty())
                continue;

            Territory t;
            t.geoPath = mapsDir + QLatin1Char('/') + geoRel;
            if (!QDir(t.geoPath).exists())
                continue;   // consta instalado pero no esta: se ignora
            t.id = geoRel.section(QLatin1Char('/'), -1);

            // Si el formato no es el que sabemos leer, mejor decirlo aqui: el
            // geocoder lo rechazaria igual al abrirlo, pero con un «no se pudo
            // abrir la base» que no explica nada. El gestor de mapas ensena el
            // mismo aviso en pantalla.
            const QString ver = entry.value(QStringLiteral("geocoder_nlp")).toObject()
                                     .value(QStringLiteral("version")).toString();
            if (!ver.isEmpty() && ver.toInt() != GeoNLP::Geocoder::version) {
                qWarning() << "OSMSCOUT:" << t.id << "es de formato v" + ver
                           << "y esta version lee v" << GeoNLP::Geocoder::version
                           << "— se ignora; hay que actualizar la app o los mapas";
                continue;
            }

            const QString postalRel =
                entry.value(QStringLiteral("postal_country")).toObject()
                     .value(QStringLiteral("path")).toString();
            if (!postalRel.isEmpty()) {
                const QString p = mapsDir + QLatin1Char('/') + postalRel;
                if (QDir(p).exists())
                    t.postalDir = p;
                else
                    qWarning() << "OSMSCOUT: faltan los datos de libpostal de" << t.id;
            }

            t.size = entry.value(QStringLiteral("geocoder_nlp")).toObject()
                          .value(QStringLiteral("size")).toString().toLongLong();
            out.append(t);
        }
    }

    // Reserva: lo instalado antes de que se escribiera countries_requested.json
    // sigue en disco y debe poder usarse, aunque sea sin datos de pais.
    if (out.isEmpty() && geoRoot.exists()) {
        const QStringList dirs = geoRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &d : dirs) {
            Territory t;
            t.id = d;
            t.geoPath = geoRoot.absoluteFilePath(d);
            const QDir sub(t.geoPath);
            for (const QFileInfo &fi : sub.entryInfoList(QDir::Files))
                t.size += fi.size();
            out.append(t);
        }
        if (!out.isEmpty())
            qWarning() << "OSMSCOUT: sin countries_requested.json; territorios "
                          "tomados del directorio, sin datos de pais de libpostal";
    }

    // De mayor a menor: el territorio grande es el que suele tener la respuesta,
    // y buscar en el primero evita abrir los demas cuando resuelve del todo.
    std::sort(out.begin(), out.end(),
              [](const Territory &a, const Territory &b) { return a.size > b.size; });
    return out;
}

bool GeoEngine::start(const QString &mapsDir, const QString &language)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    m_loaded = false;
    m_open.clear();
    m_list = readInstalled(mapsDir);

    if (m_list.isEmpty()) {
        qWarning() << "OSMSCOUT: no hay ningun territorio de busqueda instalado";
        return false;
    }

    const QString postalGlobal = mapsDir + QStringLiteral("/postal/global-v1");
    if (!QDir(postalGlobal).exists()) {
        qWarning() << "OSMSCOUT: faltan los datos globales de libpostal en" << postalGlobal;
        return false;
    }

    m_postal.set_postal_datadir(postalGlobal.toStdString(),
                                m_list.first().postalDir.toStdString());
    m_postal.add_language(language.toStdString());
    m_geocoder.set_result_language(language.toStdString());

    if (!ensureOpen(m_list.first()))
        return false;

    m_loaded = true;
    QStringList ids;
    for (const Territory &t : m_list)
        ids << t.id;
    qInfo() << "OSMSCOUT: busqueda en" << m_list.size() << "territorio(s):" << ids.join(", ");
    return true;
}

bool GeoEngine::ensureOpen(const Territory &t)
{
    if (m_open == t.id)
        return true;

    // El orden importa: libpostal analiza distinto segun el pais, asi que su
    // directorio tiene que corresponder con la base que se va a consultar.
    m_postal.set_postal_datadir_country(t.postalDir.toStdString());

    if (!m_geocoder.load(t.geoPath.toStdString())) {
        qWarning() << "OSMSCOUT: no se pudo abrir la base del geocoder:" << t.geoPath;
        m_open.clear();
        return false;
    }

    m_open = t.id;
    return true;
}

bool GeoEngine::search(const QString &pattern, size_t limit,
                       const GeoNLP::Geocoder::GeoReference &reference, bool fullResult,
                       QByteArray &result)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (!m_loaded)
        return false;

    QElapsedTimer clock;
    clock.start();

    // Un mismo pais puede repetirse entre territorios (europe-spain y
    // europe-spain-barcelona comparten el ES de libpostal), y analizar la
    // consulta no es barato: se guarda por directorio de libpostal, igual que
    // hace el original.
    struct PostalRes {
        std::vector<GeoNLP::Postal::ParseResult> parsed;
        GeoNLP::Postal::ParseResult nonorm;
    };
    QHash<QString, PostalRes> cache;

    // Se recorren TODOS los territorios, sin cortar en cuanto uno da algo. El
    // original tiene ahi un ajuste (continue_search_if_hit_found) porque puede
    // haber decenas de mapas; aqui se instalan pocos y medido en el movil son
    // ~150 ms por territorio, asi que sale a cuenta acertar siempre. Si algun
    // dia alguien instala veinte, esto se nota: el tiempo esta en el registro.
    std::vector<GeoNLP::Geocoder::GeoResult> best;
    size_t levels = 0;
    int visited = 0;

    for (const Territory &t : m_list) {
        if (!ensureOpen(t))
            continue;
        ++visited;

        PostalRes pr;
        auto cached = cache.constFind(t.postalDir);
        if (cached != cache.constEnd()) {
            pr = cached.value();
        } else {
            if (!m_postal.parse(pattern.toStdString(), pr.parsed, pr.nonorm)) {
                qWarning() << "OSMSCOUT: libpostal no pudo analizar la consulta";
                continue;
            }
            cache.insert(t.postalDir, pr);
        }

        m_geocoder.set_max_results(limit);
        std::vector<GeoNLP::Geocoder::GeoResult> found;
        // levels: no molestarse en devolver nada peor de lo que ya tenemos.
        if (!m_geocoder.search(pr.parsed, found, levels, reference)) {
            qWarning() << "OSMSCOUT: fallo la busqueda en" << t.id;
            continue;
        }
        if (found.empty())
            continue;

        // Se queda el que resuelve mas niveles de la jerarquia; si empatan, se
        // juntan y luego decide la ordenacion. Es el criterio del original.
        if (best.empty() || best[0].levels_resolved < found[0].levels_resolved) {
            best = found;
            levels = found[0].levels_resolved;
        } else if (best[0].levels_resolved == found[0].levels_resolved) {
            best.insert(best.end(), found.begin(), found.end());
        }
    }

    std::sort(best.begin(), best.end());
    dropDuplicates(best);
    if (best.size() > limit)
        best.resize(limit);

    if (m_list.size() > 1)
        qInfo() << "OSMSCOUT: busqueda en" << visited << "territorio(s),"
                << best.size() << "resultado(s)," << clock.elapsed() << "ms";

    // Mismos campos que el servidor original, para que el cliente no tenga que
    // distinguir contra cual habla.
    QJsonArray arr;
    for (const auto &sr : best) {
        QJsonObject r;
        r.insert(QStringLiteral("title"), QString::fromStdString(sr.title));
        r.insert(QStringLiteral("admin_region"), QString::fromStdString(sr.address));
        r.insert(QStringLiteral("lat"), sr.latitude);
        r.insert(QStringLiteral("lng"), sr.longitude);
        r.insert(QStringLiteral("type"), QString::fromStdString(sr.type));
        arr.append(r);
    }

    if (fullResult) {
        // /v2/search devuelve el array envuelto, que es lo que espera el cliente
        // extendido del servidor original.
        QJsonObject obj;
        obj.insert(QStringLiteral("result"), arr);
        result = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    } else {
        result = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    }

    return true;
}

bool GeoEngine::guide(const QString &poitype, const QString &name, double lat, double lon,
                      double radius, size_t limit, QByteArray &result)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (!m_loaded)
        return false;

    std::vector<std::string> type_query;
    if (!poitype.isEmpty())
        type_query.push_back(poitype.toStdString());

    if (name.isEmpty() && type_query.empty())
        return false;

    QElapsedTimer clock;
    clock.start();

    // Aqui se acumula entre territorios y se ordena por distancia al final:
    // search_nearby() añade al vector sin vaciarlo, asi que las fronteras dejan
    // de ser un corte —un POI del pais de al lado a 800 m ya aparece—.
    std::vector<GeoNLP::Geocoder::GeoResult> found;
    QHash<QString, std::vector<std::string>> nameCache;

    for (const Territory &t : m_list) {
        if (!ensureOpen(t))
            continue;

        std::vector<std::string> parsed_name;
        if (!name.isEmpty()) {
            auto cached = nameCache.constFind(t.postalDir);
            if (cached != nameCache.constEnd()) {
                parsed_name = cached.value();
            } else {
                // El nombre se expande con libpostal (sinonimos, abreviaturas);
                // el tipo va tal cual, que es una etiqueta de OSM.
                m_postal.expand_string(name.toStdString(), parsed_name);
                nameCache.insert(t.postalDir, parsed_name);
            }
        }

        // 0 = sin limite en el motor; se recorta despues, igual que el original.
        m_geocoder.set_max_results(0);
        if (!m_geocoder.search_nearby(parsed_name, type_query, lat, lon, radius, found, m_postal))
            qWarning() << "OSMSCOUT: fallo la busqueda de POIs en" << t.id;
    }

    GeoNLP::Geocoder::sort_by_distance(found.begin(), found.end());
    dropDuplicates(found);
    if (limit > 0 && found.size() > limit)
        found.resize(limit);

    if (m_list.size() > 1)
        qInfo() << "OSMSCOUT: POIs en" << m_list.size() << "territorio(s),"
                << found.size() << "resultado(s)," << clock.elapsed() << "ms";

    QJsonArray arr;
    for (const auto &sr : found) {
        QJsonObject r;
        r.insert(QStringLiteral("title"), QString::fromStdString(sr.title));
        r.insert(QStringLiteral("admin_region"), QString::fromStdString(sr.address));
        r.insert(QStringLiteral("lat"), sr.latitude);
        r.insert(QStringLiteral("lng"), sr.longitude);
        r.insert(QStringLiteral("type"), QString::fromStdString(sr.type));
        r.insert(QStringLiteral("distance"), sr.distance);
        arr.append(r);
    }

    QJsonObject obj;
    QJsonObject origin;
    origin.insert(QStringLiteral("lat"), lat);
    origin.insert(QStringLiteral("lng"), lon);
    obj.insert(QStringLiteral("origin"), origin);
    obj.insert(QStringLiteral("results"), arr);
    result = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return true;
}

bool GeoEngine::poiTypes(QByteArray &result)
{
    QFile fin(QStringLiteral(":/data/geocoder-npl-tag-aliases.json"));
    if (!fin.open(QIODevice::ReadOnly)) {
        qWarning() << "OSMSCOUT: no se pudo leer la tabla de alias de POI";
        return false;
    }

    // El fichero es {"alias2tag": {...}, "tag2alias": {"<idioma>": {"<tag>":
    // [...]}}}. Los tipos son las claves de tag2alias, iguales en todos los
    // idiomas, asi que basta con recorrer uno y quedarse con las distintas.
    const QJsonDocument doc = QJsonDocument::fromJson(fin.readAll());
    const QJsonObject byLang = doc.object().value(QStringLiteral("tag2alias")).toObject();

    QSet<QString> seen;
    for (auto lang = byLang.constBegin(); lang != byLang.constEnd(); ++lang) {
        const QJsonObject tags = lang.value().toObject();
        for (auto t = tags.constBegin(); t != tags.constEnd(); ++t)
            seen.insert(t.key());
    }

    QStringList sorted(seen.constBegin(), seen.constEnd());
    sorted.sort();
    QJsonArray types;
    for (const QString &t : sorted)
        types.append(t);

    result = QJsonDocument(types).toJson(QJsonDocument::Compact);
    return true;
}
