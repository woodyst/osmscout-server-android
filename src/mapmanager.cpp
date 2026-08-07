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

#include "mapmanager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>

#include <bzlib.h>

extern "C" {
#include "microtar.h"
}

namespace {

// El mismo servidor que usa OSM Scout Server; lo guarda en url.json junto a los
// mapas. Se deja por defecto para poder instalar sin tener nada aun.
const QString DEFAULT_SERVER = QStringLiteral("https://data.modrana.org/osm_scout_server");
const QString CATALOGUE      = QStringLiteral("countries_provided.json");
const QString INSTALLED      = QStringLiteral("countries_requested.json");

// Listas de ficheros por motor, copiadas de mapmanagerfeature.cpp del original.
const QStringList GEOCODER_FILES = {
    QStringLiteral("geonlp-primary.sqlite"),
    QStringLiteral("geonlp-normalized.trie"),
    QStringLiteral("geonlp-normalized-id.kch"),
};

/// Descomprime bzip2 en memoria. Se mira la firma antes de intentarlo: no todo
/// lo que sirve el servidor viene comprimido, y asi no hay que acertar por
/// motor cual si y cual no.
bool bunzip(const QByteArray &in, QByteArray &out)
{
    if (!in.startsWith("BZh")) {
        out = in;
        return true;
    }

    bz_stream s;
    memset(&s, 0, sizeof(s));
    if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK)
        return false;

    s.next_in = const_cast<char *>(in.constData());
    s.avail_in = uint(in.size());

    // Los mapas descomprimen a bastante mas de lo que ocupan; se crece el buffer
    // a saltos en vez de adivinar el tamano final.
    QByteArray buf;
    const int CHUNK = 1 << 20;
    int ret = BZ_OK;
    while (ret != BZ_STREAM_END) {
        const int used = buf.size();
        buf.resize(used + CHUNK);
        s.next_out = buf.data() + used;
        s.avail_out = CHUNK;
        ret = BZ2_bzDecompress(&s);
        if (ret != BZ_OK && ret != BZ_STREAM_END) {
            BZ2_bzDecompressEnd(&s);
            return false;
        }
        buf.resize(used + CHUNK - int(s.avail_out));
    }

    BZ2_bzDecompressEnd(&s);
    out = buf;
    return true;
}

} // namespace

MapManager::MapManager(const QString &mapsDir, QObject *parent)
    : QObject(parent), m_mapsDir(mapsDir), m_serverUrl(DEFAULT_SERVER)
{
    // Si ya hay mapas instalados se respeta el servidor que usaron.
    const QJsonObject url = [this] {
        QFile f(fullPath(QStringLiteral("url.json")));
        if (!f.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    const QJsonArray servers = url.value(QStringLiteral("servers")).toArray();
    if (!servers.isEmpty()) {
        const QJsonArray first = servers.first().toArray();
        if (!first.isEmpty())
            m_serverUrl = first.first().toString();
    }

    reloadTerritories();
}

/// Compone la URL de un fichero.
///
/// No basta con pegar servidor y ruta: el catalogo trae una entrada "url" que
/// dice en que directorio VERSIONADO vive cada motor —geocoder-nlp-39,
/// valhalla-34, mapboxgl-26...—, y ese numero cambia cuando cambia el formato.
/// Ademas todo se sirve comprimido, con .bz2 al final. O sea:
///
///   <base>/<url[motor]>/<path>/<fichero>.bz2
QString MapManager::featureUrl(const QString &feature, const QString &rel) const
{
    const QJsonObject url = catalogue().value(QStringLiteral("url")).toObject();
    QString base = url.value(QStringLiteral("base")).toString();
    if (base.isEmpty())
        base = m_serverUrl;
    const QString dir = url.value(feature).toString();

    return base + QLatin1Char('/') + (dir.isEmpty() ? QString() : dir + QLatin1Char('/'))
           + rel + QStringLiteral(".bz2");
}

QString MapManager::fullPath(const QString &rel) const
{
    return m_mapsDir + QLatin1Char('/') + rel;
}

QJsonObject MapManager::catalogue() const
{
    QFile f(fullPath(CATALOGUE));
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QStringList MapManager::installed() const
{
    QFile f(fullPath(INSTALLED));
    if (!f.open(QIODevice::ReadOnly))
        return QStringList();
    return QJsonDocument::fromJson(f.readAll()).object().keys();
}

/// El catalogo mezcla territorios con otras cosas: los paquetes globales
/// (mapboxgl/global, postal/global...) y una entrada "url" que no es
/// instalable. Se queda fuera solo esa ultima.
void MapManager::reloadTerritories()
{
    const QJsonObject cat = catalogue();
    m_territories.clear();
    for (const QString &k : cat.keys())
        if (cat.value(k).toObject().value(QStringLiteral("type")).toString()
            != QLatin1String("url"))
            m_territories << k;
}

void MapManager::setStatus(const QString &s, int progress)
{
    m_status = s;
    if (progress >= 0)
        m_progress = progress;
    qInfo() << "OSMSCOUT[maps]:" << s;
    emit changed();
}

void MapManager::refreshCatalogue()
{
    if (m_busy)
        return;
    m_busy = true;
    setStatus(QStringLiteral("Descargando el catálogo…"), 0);

    QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(m_serverUrl + QLatin1Char('/') + CATALOGUE)));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_busy = false;

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QStringLiteral("No se pudo descargar el catálogo: ") + reply->errorString());
            emit finished(false, m_status);
            return;
        }

        QByteArray data;
        if (!bunzip(reply->readAll(), data)) {
            setStatus(QStringLiteral("El catálogo llegó corrupto"));
            emit finished(false, m_status);
            return;
        }

        QDir().mkpath(m_mapsDir);
        QFile f(fullPath(CATALOGUE));
        if (!f.open(QIODevice::WriteOnly)) {
            setStatus(QStringLiteral("No se pudo guardar el catálogo"));
            emit finished(false, m_status);
            return;
        }
        f.write(data);
        f.close();

        reloadTerritories();
        setStatus(QStringLiteral("Catálogo: %1 territorios").arg(m_territories.size()), 100);
        emit finished(true, m_status);
    });
}

void MapManager::enqueueFeature(const QJsonObject &territory, const QString &feature,
                                const QStringList &files)
{
    const QJsonObject f = territory.value(feature).toObject();
    if (f.isEmpty())
        return;
    const QString path = f.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return;

    for (const QString &name : files) {
        Job j;
        j.url = featureUrl(feature, path + QLatin1Char('/') + name);
        j.dest = fullPath(path + QLatin1Char('/') + name);
        m_queue.enqueue(j);
    }
}

void MapManager::enqueuePackages(const QJsonObject &territory, const QString &feature,
                                 const QString &subdir)
{
    const QJsonObject f = territory.value(feature).toObject();
    if (f.isEmpty())
        return;
    const QString path = f.value(QStringLiteral("path")).toString();
    const QJsonArray packs = f.value(QStringLiteral("packages")).toArray();
    for (const QJsonValue &p : packs) {
        const QString pack = p.toString();
        // Valhalla empaqueta en .tar; los tiles vectoriales son .sqlite sueltos
        // con el nombre completo tiles-section-<pack>.sqlite.
        const QString rel = (feature == QLatin1String("valhalla"))
                                ? QStringLiteral("%1/packages/%2.tar").arg(subdir, pack)
                                : QStringLiteral("%1/tiles-section-%2.sqlite")
                                      .arg(path.isEmpty() ? subdir + QStringLiteral("/packages") : path, pack);
        Job j;
        j.url = featureUrl(feature, rel);
        j.dest = fullPath(rel);
        j.isTar = rel.endsWith(QLatin1String(".tar"));
        m_queue.enqueue(j);
    }
}

void MapManager::install(const QString &id)
{
    if (m_busy)
        return;

    const QJsonObject territory = catalogue().value(id).toObject();
    if (territory.isEmpty()) {
        setStatus(QStringLiteral("No está en el catálogo: ") + id);
        emit finished(false, m_status);
        return;
    }

    m_installing = id;
    m_queue.clear();

    enqueueFeature(territory, QStringLiteral("geocoder_nlp"), GEOCODER_FILES);
    // Los .tar de Valhalla y de los tiles se extraen; el resto son ficheros
    // sueltos que van tal cual a su sitio.
    enqueuePackages(territory, QStringLiteral("valhalla"), QStringLiteral("valhalla"));
    enqueuePackages(territory, QStringLiteral("mapboxgl_country"), QStringLiteral("mapboxgl"));

    if (m_queue.isEmpty()) {
        setStatus(QStringLiteral("Ese territorio no trae nada que instalar"));
        emit finished(false, m_status);
        return;
    }

    m_busy = true;
    m_total = m_queue.size();
    setStatus(QStringLiteral("Instalando %1: %2 ficheros").arg(id).arg(m_total), 0);
    next();
}

void MapManager::next()
{
    if (m_queue.isEmpty()) {
        m_busy = false;

        // Se anota lo instalado para que el proximo arranque sepa que hay, igual
        // que hace el original con countries_requested.json.
        QFile f(fullPath(INSTALLED));
        QJsonObject inst;
        if (f.open(QIODevice::ReadOnly)) {
            inst = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
        }
        inst.insert(m_installing, catalogue().value(m_installing).toObject());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(inst).toJson(QJsonDocument::Indented));
            f.close();
        }

        setStatus(QStringLiteral("Instalado ") + m_installing, 100);
        emit finished(true, m_status);
        return;
    }

    const Job job = m_queue.dequeue();
    setStatus(QStringLiteral("Descargando %1").arg(QFileInfo(job.dest).fileName()),
              m_total > 0 ? (m_total - m_queue.size() - 1) * 100 / m_total : 0);

    QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(job.url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_busy = false;
            setStatus(QStringLiteral("Falló %1: %2")
                          .arg(QFileInfo(job.dest).fileName(), reply->errorString()));
            emit finished(false, m_status);
            return;
        }

        if (!writeDecompressed(reply->readAll(), job.dest)) {
            m_busy = false;
            setStatus(QStringLiteral("No se pudo guardar ") + job.dest);
            emit finished(false, m_status);
            return;
        }

        if (job.isTar) {
            const QString dir = QFileInfo(job.dest).absolutePath() + QStringLiteral("/..");
            if (!extractTar(job.dest, QDir(dir).absolutePath())) {
                m_busy = false;
                setStatus(QStringLiteral("No se pudo extraer ") + job.dest);
                emit finished(false, m_status);
                return;
            }
            // El .tar ya no hace falta y ocupa lo mismo que lo que contiene.
            QFile::remove(job.dest);
        }

        next();
    });
}

bool MapManager::writeDecompressed(const QByteArray &data, const QString &dest)
{
    QByteArray plain;
    if (!bunzip(data, plain))
        return false;

    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(plain) == plain.size();
}

bool MapManager::extractTar(const QString &tarPath, const QString &destDir)
{
    mtar_t tar;
    if (mtar_open(&tar, tarPath.toUtf8().constData(), "r") != MTAR_ESUCCESS) {
        qWarning() << "OSMSCOUT[maps]: no se pudo abrir el tar" << tarPath;
        return false;
    }

    mtar_header_t h;
    bool ok = true;
    while (mtar_read_header(&tar, &h) == MTAR_ESUCCESS) {
        const QString name = QString::fromUtf8(h.name);

        // Los .tar del servidor traen rutas relativas; una con ".." escaparia
        // del directorio de mapas, asi que se descarta.
        if (name.contains(QStringLiteral(".."))) {
            qWarning() << "OSMSCOUT[maps]: ruta sospechosa en el tar:" << name;
            ok = false;
            break;
        }

        const QString out = destDir + QLatin1Char('/') + name;
        if (h.type == MTAR_TDIR) {
            QDir().mkpath(out);
        } else if (h.type == MTAR_TREG) {
            QDir().mkpath(QFileInfo(out).absolutePath());
            QByteArray buf(int(h.size), Qt::Uninitialized);
            if (mtar_read_data(&tar, buf.data(), h.size) != MTAR_ESUCCESS) {
                ok = false;
                break;
            }
            QFile f(out);
            if (!f.open(QIODevice::WriteOnly) || f.write(buf) != buf.size()) {
                ok = false;
                break;
            }
        }

        if (mtar_next(&tar) != MTAR_ESUCCESS)
            break;
    }

    mtar_close(&tar);
    return ok;
}
