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
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSet>

#include <bzlib.h>

#include "geocoder.h"   // GeoNLP::Geocoder::version

extern "C" {
#include "microtar.h"
}

namespace {

// El mismo servidor que usa OSM Scout Server; lo guarda en url.json junto a los
// mapas. Se deja por defecto para poder instalar sin tener nada aun.
const QString DEFAULT_SERVER = QStringLiteral("https://data.modrana.org/osm_scout_server");
const QString CATALOGUE      = QStringLiteral("countries_provided.json");
const QString INSTALLED      = QStringLiteral("countries_requested.json");
// Lo que falta por bajar del territorio en curso. No lo tiene el original: alli
// no hay app que el sistema pueda matar a mitad de una descarga de 3,5 GB.
const QString PENDING        = QStringLiteral("downloads_pending.json");

// Listas de ficheros por motor, copiadas de mapmanagerfeature.cpp del original.
const QStringList GEOCODER_FILES = {
    QStringLiteral("geonlp-primary.sqlite"),
    QStringLiteral("geonlp-normalized.trie"),
    QStringLiteral("geonlp-normalized-id.kch"),
};
const QStringList POSTAL_GLOBAL_FILES = {
    QStringLiteral("address_expansions/address_dictionary.dat"),
    QStringLiteral("language_classifier/language_classifier.dat"),
    QStringLiteral("numex/numex.dat"),
    QStringLiteral("transliteration/transliteration.dat"),
};
const QStringList POSTAL_COUNTRY_FILES = {
    QStringLiteral("address_parser/address_parser_crf.dat"),
    QStringLiteral("address_parser/address_parser_phrases.dat"),
    QStringLiteral("address_parser/address_parser_postal_codes.dat"),
    QStringLiteral("address_parser/address_parser_vocab.trie"),
};
const QStringList MAPBOXGL_WORLD_FILES  = { QStringLiteral("tiles-world.sqlite") };
const QStringList MAPBOXGL_GLYPHS_FILES = { QStringLiteral("glyphs.sqlite") };

/// Version de formato que sabe leer cada motor. Los numeros son los del
/// original (mapmanagerfeature.cpp); el del geocoder se toma de su propia
/// constante para que no se puedan desincronizar.
///
/// El catalogo trae la version de cada cosa que sirve. Cuando rinigus cambia un
/// formato sube ese numero, y los datos nuevos dejan de valer para un binario
/// viejo —y al reves—. Sin comprobarlo, la unica senal era que el motor no
/// cargaba: el geocoder responde con «no se pudo abrir la base», y Valhalla
/// simplemente no encuentra rutas. Peor aun, se habrian bajado los gigas antes
/// de enterarse.
const QHash<QString, int> FEATURE_VERSION = {
    { QStringLiteral("geocoder_nlp"),     GeoNLP::Geocoder::version },
    { QStringLiteral("postal_global"),    2 },
    { QStringLiteral("postal_country"),   2 },
    { QStringLiteral("mapboxgl_global"),  3 },
    { QStringLiteral("mapboxgl_glyphs"),  1 },
    { QStringLiteral("mapboxgl_country"), 3 },
    { QStringLiteral("valhalla"),         2 },
};

/// Devuelve los motores de esta entrada cuyo formato no sabemos leer, ya
/// descritos para poder ensenarlos. Vacio = todo en orden.
///
/// Sirve igual para una entrada del catalogo (antes de bajar) que para una de
/// countries_requested.json (lo ya instalado), porque el Map Manager guarda la
/// entrada entera tal cual.
QStringList incompatibles(const QJsonObject &entry)
{
    QStringList mal;
    for (auto it = FEATURE_VERSION.constBegin(); it != FEATURE_VERSION.constEnd(); ++it) {
        const QJsonObject f = entry.value(it.key()).toObject();
        if (f.isEmpty())
            continue;   // esa entrada no trae ese motor: no hay nada que comprobar
        const QString v = f.value(QStringLiteral("version")).toString();
        if (v.isEmpty())
            continue;   // sin version declarada, se deja pasar como el original
        if (v.toInt() != it.value())
            mal << QStringLiteral("%1 (v%2, aquí v%3)").arg(it.key(), v).arg(it.value());
    }
    mal.sort();
    return mal;
}

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

    // Si la vez anterior se corto una instalacion, aqui queda constancia y la
    // interfaz puede ofrecer seguir o descartar.
    QFile p(fullPath(PENDING));
    if (p.open(QIODevice::ReadOnly)) {
        m_pending = QJsonDocument::fromJson(p.readAll()).object()
                        .value(QStringLiteral("id")).toString();
        if (!m_pending.isEmpty())
            qInfo() << "OSMSCOUT[maps]: quedo a medias" << m_pending;
    }
}

void MapManager::saveProgress()
{
    QJsonArray restantes;
    for (const Job &j : m_queue) {
        QJsonObject o;
        o.insert(QStringLiteral("url"), j.url);
        o.insert(QStringLiteral("dest"), j.dest);
        o.insert(QStringLiteral("isTar"), j.isTar);
        restantes.append(o);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), m_installing);
    obj.insert(QStringLiteral("total"), m_total);
    obj.insert(QStringLiteral("restantes"), restantes);

    QDir().mkpath(m_mapsDir);
    QFile f(fullPath(PENDING));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void MapManager::clearProgress()
{
    QFile::remove(fullPath(PENDING));
    m_pending.clear();
}

bool MapManager::loadProgress()
{
    QFile f(fullPath(PENDING));
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_installing = obj.value(QStringLiteral("id")).toString();
    m_total = obj.value(QStringLiteral("total")).toInt();
    m_queue.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("restantes")).toArray()) {
        const QJsonObject o = v.toObject();
        Job j;
        j.url = o.value(QStringLiteral("url")).toString();
        j.dest = o.value(QStringLiteral("dest")).toString();
        j.isTar = o.value(QStringLiteral("isTar")).toBool();
        m_queue.enqueue(j);
    }
    return !m_installing.isEmpty() && !m_queue.isEmpty();
}

bool MapManager::yaEsta(const Job &job) const
{
    // Del .tar no queda el .tar —se borra al extraerlo— sino su listado, que es
    // ademas lo que hace falta para poder desinstalarlo despues.
    return job.isTar ? QFile::exists(job.dest + QStringLiteral(".list"))
                     : QFile::exists(job.dest);
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

QString MapManager::formatWarning() const
{
    QFile f(fullPath(INSTALLED));
    if (!f.open(QIODevice::ReadOnly))
        return QString();

    const QJsonObject inst = QJsonDocument::fromJson(f.readAll()).object();
    QStringList afectados;
    for (const QString &id : inst.keys())
        if (!incompatibles(inst.value(id).toObject()).isEmpty())
            afectados << id;

    if (afectados.isEmpty())
        return QString();

    // El detalle de que motor y que version va al registro; en pantalla, lo que
    // el usuario puede hacer.
    for (const QString &id : afectados)
        qWarning() << "OSMSCOUT[maps]: formato no soportado en" << id
                   << incompatibles(inst.value(id).toObject());

    return QStringLiteral("%1 se descargó con un formato que esta versión no "
                          "lee. Actualiza la aplicación, o desinstala y vuelve "
                          "a descargar.").arg(afectados.join(QStringLiteral(", ")));
}

QStringList MapManager::installed() const
{
    QFile f(fullPath(INSTALLED));
    if (!f.open(QIODevice::ReadOnly))
        return QStringList();
    return QJsonDocument::fromJson(f.readAll()).object().keys();
}

/// El catalogo mezcla territorios con otras cosas. Se filtran dos:
///
///   - la entrada "url", que no es instalable: dice donde vive cada motor.
///   - todo lo de mapnik, que son tiles RASTER. Este port no lleva mapnik
///     —Navius usa tiles vectoriales— asi que ofrecerlos seria invitar a
///     descargar cientos de MB que no se van a usar.
///
/// Los demas globales SI se quedan, aunque no sean territorios:
/// mapboxgl/glyphs son las fuentes con las que se rotula el mapa y
/// postal/global los datos de normalizacion de direcciones. Sin ellos el mapa
/// sale sin nombres y la busqueda no entiende las consultas.
void MapManager::reloadTerritories()
{
    const QJsonObject cat = catalogue();
    m_territories.clear();
    for (const QString &k : cat.keys()) {
        if (cat.value(k).toObject().value(QStringLiteral("type")).toString()
            == QLatin1String("url"))
            continue;
        if (k.startsWith(QLatin1String("mapnik")))
            continue;
        // Los globales —glyphs, mundo, postal— no se eligen: se instalan solos
        // como dependencia del primer territorio que los necesite. Ver install().
        if (cat.value(k).toObject().value(QStringLiteral("type")).toString()
            != QLatin1String("territory"))
            continue;
        m_territories << k;
    }
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

/// Encola un paquete global solo si alguno de sus ficheros no esta ya en disco.
/// Asi instalar un segundo territorio no vuelve a bajar los 100 MB del mundo ni
/// los 72 MB de fuentes.
void MapManager::enqueueGlobalIfMissing(const QJsonObject &cat, const QString &id,
                                        const QString &feature, const QStringList &files)
{
    const QJsonObject entry = cat.value(id).toObject();
    const QJsonObject f = entry.value(feature).toObject();
    const QString path = f.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return;

    for (const QString &name : files) {
        const QString dest = fullPath(path + QLatin1Char('/') + name);
        if (QFile::exists(dest))
            continue;
        Job j;
        j.url = featureUrl(feature, path + QLatin1Char('/') + name);
        j.dest = dest;
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

    // Antes de bajar nada: si el servidor sirve un formato que esta version no
    // sabe leer, descargarlo son gigas tirados. Se para aqui y se dice por que.
    const QStringList mal = incompatibles(territory);
    if (!mal.isEmpty()) {
        setStatus(QStringLiteral("%1 usa un formato que esta versión no lee: %2. "
                                 "Hay que actualizar la aplicación.")
                      .arg(id, mal.join(QStringLiteral(", "))));
        emit finished(false, m_status);
        return;
    }

    m_installing = id;
    m_queue.clear();

    // Los globales van primero y solo si faltan: son dependencias del
    // territorio, no algo que el usuario tenga que elegir. Sin los glyphs el
    // mapa sale sin nombres, y sin los datos de postal la busqueda no entiende
    // las consultas.
    const QJsonObject cat = catalogue();
    enqueueGlobalIfMissing(cat, QStringLiteral("mapboxgl/glyphs"),
                           QStringLiteral("mapboxgl_glyphs"), MAPBOXGL_GLYPHS_FILES);
    enqueueGlobalIfMissing(cat, QStringLiteral("mapboxgl/global"),
                           QStringLiteral("mapboxgl_global"), MAPBOXGL_WORLD_FILES);
    enqueueGlobalIfMissing(cat, QStringLiteral("postal/global"),
                           QStringLiteral("postal_global"), POSTAL_GLOBAL_FILES);

    enqueueFeature(territory, QStringLiteral("geocoder_nlp"), GEOCODER_FILES);
    enqueueFeature(territory, QStringLiteral("postal_country"), POSTAL_COUNTRY_FILES);
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
    m_pending = id;
    // Antes de bajar el primer byte: si se corta enseguida, tiene que constar.
    saveProgress();
    setStatus(QStringLiteral("Instalando %1: %2 ficheros").arg(id).arg(m_total), 0);
    next();
}

void MapManager::resume()
{
    if (m_busy)
        return;

    if (!loadProgress()) {
        clearProgress();
        setStatus(QStringLiteral("No había nada que reanudar"));
        emit changed();
        return;
    }

    m_busy = true;
    m_pending = m_installing;
    setStatus(QStringLiteral("Reanudando %1: quedan %2 ficheros")
                  .arg(m_installing).arg(m_queue.size()), 0);
    next();
}

void MapManager::discard()
{
    if (m_busy)
        return;

    if (!loadProgress()) {
        clearProgress();
        emit changed();
        return;
    }

    const QString id = m_installing;
    const QJsonObject mio = catalogue().value(id).toObject();

    // Lo instalado y completo manda: si un paquete lo comparte un territorio que
    // si esta instalado, no se toca.
    QFile f(fullPath(INSTALLED));
    QJsonObject inst;
    if (f.open(QIODevice::ReadOnly)) {
        inst = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }

    const int borrados = borrarDatos(mio, inst);

    // Y los .part que hayan quedado a medias por ahi.
    int trozos = 0;
    QDirIterator it(m_mapsDir, QStringList() << QStringLiteral("*.part"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        if (QFile::remove(it.next()))
            ++trozos;

    m_queue.clear();
    m_installing.clear();
    clearProgress();

    setStatus(QStringLiteral("Descartado %1 (%2 elementos, %3 a medias)")
                  .arg(id).arg(borrados).arg(trozos));
    emit changed();
    emit installedChanged();
    emit finished(true, m_status);
}

void MapManager::next()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        clearProgress();

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
        emit installedChanged();
        emit finished(true, m_status);
        return;
    }

    const Job job = m_queue.dequeue();

    // Lo que ya esta en disco no se vuelve a bajar. Sirve para dos cosas a la
    // vez: reanudar una descarga cortada, y no repetir los paquetes que
    // comparten los territorios vecinos —instalar Andorra teniendo Espana
    // volvia a bajar 238 MB de tiles que ya estaban—.
    if (yaEsta(job)) {
        setStatus(QStringLiteral("Ya estaba: %1").arg(QFileInfo(job.dest).fileName()),
                  m_total > 0 ? (m_total - m_queue.size()) * 100 / m_total : 0);
        saveProgress();
        // Sin recursion directa: una cola de miles de ficheros ya presentes
        // desbordaria la pila.
        QMetaObject::invokeMethod(this, [this] { next(); }, Qt::QueuedConnection);
        return;
    }

    setStatus(QStringLiteral("Descargando %1").arg(QFileInfo(job.dest).fileName()),
              m_total > 0 ? (m_total - m_queue.size() - 1) * 100 / m_total : 0);

    if (!streamStart(job.dest)) {
        m_busy = false;
        setStatus(QStringLiteral("No se pudo crear ") + job.dest);
        emit finished(false, m_status);
        return;
    }

    QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(job.url)));

    // Se escribe segun llega, sin acumular la respuesta: es la diferencia entre
    // usar unos pocos MB y medio giga por fichero.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (!streamFeed(reply->readAll()))
            reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            streamAbort();
            m_busy = false;
            // El que ha fallado vuelve a la cola y se anota: asi «Reanudar»
            // empieza por el, no por el siguiente.
            m_queue.prepend(job);
            saveProgress();
            m_pending = m_installing;
            setStatus(QStringLiteral("Falló %1: %2")
                          .arg(QFileInfo(job.dest).fileName(), reply->errorString()));
            emit finished(false, m_status);
            return;
        }

        if (!streamFinish(job.dest)) {
            m_busy = false;
            setStatus(QStringLiteral("No se pudo guardar ") + job.dest);
            emit finished(false, m_status);
            return;
        }

        if (job.isTar) {
            // A la RAIZ de los mapas, no al directorio del paquete: los .tar ya
            // traen dentro rutas del tipo "valhalla/tiles/0/…", asi que
            // extraerlos en valhalla/ dejaba un valhalla/valhalla/tiles que el
            // motor no encuentra. No se noto hasta instalar desde cero, porque
            // el directorio bueno ya existia de una copia anterior.
            if (!extractTar(job.dest, m_mapsDir)) {
                m_busy = false;
                setStatus(QStringLiteral("No se pudo extraer ") + job.dest);
                emit finished(false, m_status);
                return;
            }
            // El .tar ya no hace falta y ocupa lo mismo que lo que contiene.
            QFile::remove(job.dest);
        }

        // Despues de CADA fichero, no al final. Es la diferencia entre poder
        // reanudar y quedarse con gigas huerfanos si Android mata el proceso.
        saveProgress();
        next();
    });
}

bool MapManager::streamStart(const QString &dest)
{
    QDir().mkpath(QFileInfo(dest).absolutePath());
    m_out.setFileName(dest + QStringLiteral(".part"));
    if (!m_out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    m_head.clear();
    m_bzDecided = false;
    m_bzActive = false;
    return true;
}

bool MapManager::streamFeed(const QByteArray &chunk)
{
    if (!m_out.isOpen())
        return false;

    QByteArray in = chunk;

    // Hasta tener tres bytes no se sabe si viene comprimido, asi que se
    // retienen. No todo lo del servidor es .bz2 y esto evita tener que acertar
    // por motor cual si y cual no.
    if (!m_bzDecided) {
        m_head += in;
        if (m_head.size() < 3)
            return true;
        in = m_head;
        m_head.clear();
        m_bzDecided = true;
        if (in.startsWith("BZh")) {
            memset(&m_bz, 0, sizeof(m_bz));
            if (BZ2_bzDecompressInit(&m_bz, 0, 0) != BZ_OK)
                return false;
            m_bzActive = true;
        }
    }

    if (!m_bzActive)
        return m_out.write(in) == in.size();

    m_bz.next_in = const_cast<char *>(in.constData());
    m_bz.avail_in = uint(in.size());

    QByteArray buf(1 << 20, Qt::Uninitialized);
    while (m_bz.avail_in > 0) {
        m_bz.next_out = buf.data();
        m_bz.avail_out = uint(buf.size());
        const int ret = BZ2_bzDecompress(&m_bz);
        if (ret != BZ_OK && ret != BZ_STREAM_END)
            return false;
        const qint64 got = buf.size() - m_bz.avail_out;
        if (got > 0 && m_out.write(buf.constData(), got) != got)
            return false;
        if (ret == BZ_STREAM_END)
            break;
    }
    return true;
}

bool MapManager::streamFinish(const QString &dest)
{
    // Un fichero mas corto que la firma nunca llego a decidirse; se vuelca tal
    // cual para no perderlo.
    if (!m_bzDecided && !m_head.isEmpty())
        m_out.write(m_head);

    if (m_bzActive) {
        BZ2_bzDecompressEnd(&m_bz);
        m_bzActive = false;
    }
    m_out.close();

    // Solo al final se renombra: asi un corte a mitad no deja un fichero de
    // mapas truncado con nombre bueno, que luego fallaria al cargar.
    QFile::remove(dest);
    return QFile::rename(m_out.fileName(), dest);
}

void MapManager::streamAbort()
{
    if (m_bzActive) {
        BZ2_bzDecompressEnd(&m_bz);
        m_bzActive = false;
    }
    if (m_out.isOpen())
        m_out.close();
    QFile::remove(m_out.fileName());
}

bool MapManager::extractTar(const QString &tarPath, const QString &destDir)
{
    mtar_t tar;
    if (mtar_open(&tar, tarPath.toUtf8().constData(), "r") != MTAR_ESUCCESS) {
        qWarning() << "OSMSCOUT[maps]: no se pudo abrir el tar" << tarPath;
        return false;
    }

    mtar_header_t h;
    QStringList listado;
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
        listado << name;
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

    // Se guarda que trajo cada paquete: sin esto no hay forma de desinstalar un
    // territorio sin llevarse por delante ficheros de otro, porque los paquetes
    // de zonas fronterizas se comparten. Es lo que hace el original con sus
    // .tar.list.
    if (ok) {
        QFile lf(tarPath + QStringLiteral(".list"));
        if (lf.open(QIODevice::WriteOnly | QIODevice::Text))
            lf.write(listado.join(QLatin1Char('\n')).toUtf8());
    }

    return ok;
}

/// Desinstala un territorio.
///
/// Borra lo suyo propio —geocoder y datos de pais de libpostal— y, de los
/// paquetes, solo los que no use ningun otro territorio instalado: las zonas
/// fronterizas comparten tiles, y borrar a lo bruto deja huecos en el mapa o en
/// las rutas de un pais vecino, que es de lo mas dificil de diagnosticar.
///
/// Los globales (fuentes, mundo, postal) no se tocan: valen para todos.
void MapManager::uninstall(const QString &id)
{
    if (m_busy)
        return;

    QFile f(fullPath(INSTALLED));
    QJsonObject inst;
    if (f.open(QIODevice::ReadOnly)) {
        inst = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
    const QJsonObject mio = inst.value(id).toObject();
    if (mio.isEmpty())
        return;

    QJsonObject otros = inst;
    otros.remove(id);
    const int borrados = borrarDatos(mio, otros);

    inst.remove(id);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(inst).toJson(QJsonDocument::Indented));
        f.close();
    }

    setStatus(QStringLiteral("Desinstalado %1 (%2 elementos)").arg(id).arg(borrados));
    emit changed();
    emit installedChanged();
    emit finished(true, m_status);
}

int MapManager::borrarDatos(const QJsonObject &mio, const QJsonObject &otros)
{
    if (mio.isEmpty())
        return 0;

    // Paquetes que siguen haciendo falta para los demas.
    QSet<QString> enUso;
    for (const QString &otro : otros.keys()) {
        const QJsonObject o = otros.value(otro).toObject();
        for (const QString &feat : { QStringLiteral("valhalla"),
                                     QStringLiteral("mapboxgl_country") })
            for (const QJsonValue &p : o.value(feat).toObject()
                                            .value(QStringLiteral("packages")).toArray())
                enUso.insert(feat + QLatin1Char(':') + p.toString());
    }

    int borrados = 0;

    // Lo exclusivo del territorio: directorios enteros.
    for (const QString &feat : { QStringLiteral("geocoder_nlp"),
                                 QStringLiteral("postal_country") }) {
        const QString path = mio.value(feat).toObject()
                                 .value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && QDir(fullPath(path)).removeRecursively())
            ++borrados;
    }

    // Los paquetes, uno a uno y solo si nadie mas los usa.
    for (const QJsonValue &p : mio.value(QStringLiteral("valhalla")).toObject()
                                   .value(QStringLiteral("packages")).toArray()) {
        const QString pack = p.toString();
        if (enUso.contains(QStringLiteral("valhalla:") + pack))
            continue;
        const QString lista = fullPath(QStringLiteral("valhalla/packages/%1.tar.list").arg(pack));
        QFile lf(lista);
        if (!lf.open(QIODevice::ReadOnly))
            continue;   // instalado antes de guardar listas: se deja, no se adivina
        for (const QString &rel : QString::fromUtf8(lf.readAll()).split(QLatin1Char('\n')))
            if (!rel.trimmed().isEmpty() && QFile::remove(fullPath(rel.trimmed())))
                ++borrados;
        lf.close();
        QFile::remove(lista);
    }

    for (const QJsonValue &p : mio.value(QStringLiteral("mapboxgl_country")).toObject()
                                   .value(QStringLiteral("packages")).toArray()) {
        const QString pack = p.toString();
        if (enUso.contains(QStringLiteral("mapboxgl_country:") + pack))
            continue;
        if (QFile::remove(fullPath(QStringLiteral("mapboxgl/packages/tiles-section-%1.sqlite").arg(pack))))
            ++borrados;
    }

    return borrados;
}
