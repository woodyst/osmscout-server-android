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
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

GeoEngine::GeoEngine() = default;

bool GeoEngine::running() const
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_loaded;
}

bool GeoEngine::start(const QString &mapsDir, const QString &language,
                      const QString &territory)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    m_loaded = false;

    const QDir geoRoot(mapsDir + QStringLiteral("/geocoder-nlp"));
    if (!geoRoot.exists()) {
        qWarning() << "OSMSCOUT: no hay datos de geocoder en" << geoRoot.absolutePath();
        return false;
    }

    // Los territorios vienen como europe-spain, europe-andorra,
    // europe-spain-barcelona... geocoder-nlp carga UNA base a la vez, asi que
    // hay que elegir. Sin seleccion de mapa todavia, se coge la MAS GRANDE, que
    // es la que cubre mas territorio: por orden alfabetico saldria Andorra, que
    // es la menos util de las instaladas.
    QString chosen = territory;
    if (chosen.isEmpty()) {
        qint64 best = -1;
        const QStringList dirs = geoRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &d : dirs) {
            qint64 size = 0;
            const QDir sub(geoRoot.absoluteFilePath(d));
            for (const QFileInfo &fi : sub.entryInfoList(QDir::Files))
                size += fi.size();
            if (size > best) {
                best = size;
                chosen = d;
            }
        }
        if (chosen.isEmpty()) {
            qWarning() << "OSMSCOUT: no hay ningun territorio en" << geoRoot.absolutePath();
            return false;
        }
    }

    // libpostal necesita los datos globales y, aparte, los del pais. El
    // directorio de pais sale del codigo ISO, que el Map Manager guarda en
    // mayusculas: postal/countries-v1/ES.
    const QString postalGlobal = mapsDir + QStringLiteral("/postal/global-v1");
    // El codigo de pais sale del nombre del territorio. Es una tabla corta a
    // proposito: el original lo saca de los metadatos del Map Manager
    // (countries_requested.json), que aqui todavia no se leen. Si no hay
    // correspondencia se sigue sin datos de pais, que libpostal admite.
    static const QHash<QString, QString> kCountryOf = {
        { QStringLiteral("europe-spain"),   QStringLiteral("ES") },
        { QStringLiteral("europe-andorra"), QStringLiteral("AD") },
        { QStringLiteral("europe-france"),  QStringLiteral("FR") },
        { QStringLiteral("europe-portugal"), QStringLiteral("PT") },
    };
    QString code;
    for (auto it = kCountryOf.constBegin(); it != kCountryOf.constEnd(); ++it)
        if (chosen.startsWith(it.key())) { code = it.value(); break; }

    QString postalCountry;
    if (!code.isEmpty()) {
        const QString c = mapsDir + QStringLiteral("/postal/countries-v1/") + code;
        if (QDir(c).exists())
            postalCountry = c;
        else
            qWarning() << "OSMSCOUT: no hay datos de libpostal para" << code;
    }

    if (!QDir(postalGlobal).exists()) {
        qWarning() << "OSMSCOUT: faltan los datos globales de libpostal en" << postalGlobal;
        return false;
    }

    m_postal.set_postal_datadir(postalGlobal.toStdString(), postalCountry.toStdString());
    m_postal.add_language(language.toStdString());

    m_geocoder.set_result_language(language.toStdString());

    const QString geopath = geoRoot.absoluteFilePath(chosen);
    if (!m_geocoder.load(geopath.toStdString())) {
        qWarning() << "OSMSCOUT: no se pudo abrir la base del geocoder:" << geopath;
        return false;
    }

    m_territory = chosen;
    m_loaded = true;
    qInfo() << "OSMSCOUT: geocoder cargado" << chosen << "postal" << postalCountry;
    return true;
}

bool GeoEngine::search(const QString &pattern, size_t limit,
                       const GeoNLP::Geocoder::GeoReference &reference, bool fullResult,
                       QByteArray &result)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (!m_loaded)
        return false;

    // libpostal trocea y normaliza: "carrer de balmes 12 barcelona" se convierte
    // en una jerarquia de calle/numero/ciudad que es lo que el geocoder busca.
    std::vector<GeoNLP::Postal::ParseResult> parsed;
    GeoNLP::Postal::ParseResult nonorm;
    if (!m_postal.parse(pattern.toStdString(), parsed, nonorm)) {
        qWarning() << "OSMSCOUT: libpostal no pudo analizar la consulta";
        return false;
    }

    m_geocoder.set_max_results(limit);
    std::vector<GeoNLP::Geocoder::GeoResult> found;
    if (!m_geocoder.search(parsed, found, 0, reference)) {
        qWarning() << "OSMSCOUT: fallo la busqueda en geocoder-nlp";
        return false;
    }

    // Mismos campos que el servidor original, para que el cliente no tenga que
    // distinguir contra cual habla.
    QJsonArray arr;
    for (const auto &sr : found) {
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

    // El nombre se expande con libpostal (sinonimos, abreviaturas); el tipo va
    // tal cual, que es una etiqueta de OSM.
    std::vector<std::string> parsed_name;
    if (!name.isEmpty())
        m_postal.expand_string(name.toStdString(), parsed_name);

    std::vector<std::string> type_query;
    if (!poitype.isEmpty())
        type_query.push_back(poitype.toStdString());

    if (parsed_name.empty() && type_query.empty())
        return false;

    // 0 = sin limite en el motor; se recorta despues, igual que el original.
    m_geocoder.set_max_results(0);

    std::vector<GeoNLP::Geocoder::GeoResult> found;
    if (!m_geocoder.search_nearby(parsed_name, type_query, lat, lon, radius, found, m_postal)) {
        qWarning() << "OSMSCOUT: fallo la busqueda de POIs";
        return false;
    }

    QJsonArray arr;
    size_t n = 0;
    for (const auto &sr : found) {
        if (limit > 0 && n++ >= limit)
            break;
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
