/******************************************************************************
 *
 * Copyright (C) 2020 Marton Borzak <hello@martonborzak.com>
 *
 * This file is part of the YIO-Remote software project.
 *
 * YIO-Remote software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * YIO-Remote software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with YIO-Remote software. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *****************************************************************************/

#include "bangolufsen.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtDebug>

#include "yio-interface/entities/mediaplayerinterface.h"

BangOlufsenPlugin::BangOlufsenPlugin() : Plugin("bangolufsen", USE_WORKER_THREAD) {}

Integration *BangOlufsenPlugin::createIntegration(const QVariantMap &config, EntitiesInterface *entities,
                                                  NotificationsInterface *notifications, YioAPIInterface *api,
                                                  ConfigInterface *configObj) {
    qCInfo(m_logCategory) << "Creating Bang&Olufsen integration plugin" << PLUGIN_VERSION;

    return new BangOlufsen(config, entities, notifications, api, configObj, this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT THREAD CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BangOlufsen::BangOlufsen(const QVariantMap &config, EntitiesInterface *entities, NotificationsInterface *notifications,
                         YioAPIInterface *api, ConfigInterface *configObj, Plugin *plugin)
    : Integration(config, entities, notifications, api, configObj, plugin) {
    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == Integration::OBJ_DATA) {
            QVariantMap map = iter.value().toMap();
            m_ip            = map.value(Integration::KEY_DATA_IP).toString();
            m_baseUrl       = QString("http://").append(m_ip).append(":8080");
            m_entityId      = map.value(Integration::KEY_ENTITY_ID).toString();
        }
    }

    // set up polling timer
    m_pollingTimer = new QTimer(this);
    m_pollingTimer->setInterval(10000);
    QObject::connect(m_pollingTimer, &QTimer::timeout, this, &BangOlufsen::onPollingTimerTimeout);

    // add available entity
    QStringList supportedFeatures;
    supportedFeatures << "SOURCE"
                      << "APP_NAME"
                      << "VOLUME"
                      << "VOLUME_UP"
                      << "VOLUME_DOWN"
                      << "VOLUME_SET"
                      << "MUTE"
                      << "MUTE_SET"
                      << "MEDIA_TYPE"
                      << "MEDIA_TITLE"
                      << "MEDIA_ARTIST"
                      << "MEDIA_ALBUM"
                      << "MEDIA_DURATION"
                      << "MEDIA_POSITION"
                      << "MEDIA_IMAGE"
                      << "PLAY"
                      << "PAUSE"
                      << "STOP"
                      << "PREVIOUS"
                      << "NEXT"
                      << "SEEK"
                      << "SHUFFLE"
                      << "TURN_ON"
                      << "TURN_OFF";
    addAvailableEntity(m_entityId, "media_player", integrationId(), friendlyName(), supportedFeatures);

    getStandby();
}

void BangOlufsen::updateEntity(const QString &entity_id, const QVariantMap &map) {
    EntityInterface *entity = m_entities->getEntityInterface(entity_id);
    if (entity) {
        QString state = getState(map);
        if (state == "play") {
            entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::PLAYING);
        } else if (state == "pause") {
            entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::IDLE);
        } else if (state == "stop") {
            entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::IDLE);
        }

        QString source = getSource(map);
        if (entity->isSupported(MediaPlayerDef::F_SOURCE) && !source.isEmpty()) {
            entity->updateAttrByIndex(MediaPlayerDef::SOURCE, source);
        }

        int volume = getVolume(map);
        if (entity->isSupported(MediaPlayerDef::F_VOLUME_SET) && volume != -1) {
            entity->updateAttrByIndex(MediaPlayerDef::VOLUME, volume);
        }

        bool muted = getMuted(map);
        if (entity->isSupported(MediaPlayerDef::F_MUTE_SET) && entity->isSupported(MediaPlayerDef::F_MUTE)) {
            entity->updateAttrByIndex(MediaPlayerDef::MUTED, muted);
        }

        QVariantMap musicInfo = getMusicInfo(map);
        //        // media type
        //        if (entity->isSupported(MediaPlayerDef::F_MEDIA_TYPE) && haAttr.contains("media_content_type")) {
        //            entity->updateAttrByIndex(MediaPlayerDef::MEDIATYPE,
        //            haAttr.value("media_content_type").toString());
        //        }

        // media image
        if (entity->isSupported(MediaPlayerDef::F_MEDIA_IMAGE) && musicInfo.contains("mediaUrl")) {
            entity->updateAttrByIndex(MediaPlayerDef::MEDIAIMAGE, musicInfo.value("mediaUrl").toString());
        }

        // media title
        if (entity->isSupported(MediaPlayerDef::F_MEDIA_TITLE) && musicInfo.contains("mediaTrack")) {
            entity->updateAttrByIndex(MediaPlayerDef::MEDIATITLE, musicInfo.value("mediaTrack").toString());
        }

        // media artist
        if (entity->isSupported(MediaPlayerDef::F_MEDIA_ARTIST) && musicInfo.contains("mediaArtist")) {
            entity->updateAttrByIndex(MediaPlayerDef::MEDIAARTIST, musicInfo.value("mediaArtist").toString());
        }

        // media duration
        int duration = getDuration(map);
        if (entity->isSupported(MediaPlayerDef::F_MEDIA_DURATION)) {
            entity->updateAttrByIndex(MediaPlayerDef::MEDIADURATION, duration);
        }

        // media position
        int positon = getPosition(map);
        if (entity->isSupported(MediaPlayerDef::F_MEDIA_POSITION)) {
            entity->updateAttrByIndex(MediaPlayerDef::MEDIAPROGRESS, positon);
        }
    }
}

void BangOlufsen::connect() {
    if (!m_pollingTimer->isActive()) {
        m_pollingTimer->start();
    }

    if (m_state != CONNECTED || m_state != CONNECTING) {
        setState(CONNECTING);
        m_userDisconnect = false;
        qCDebug(m_logCategory) << "Connecting to a Bang & Olufsen product:" << m_baseUrl;

        m_manager = new QNetworkAccessManager(this);

        QNetworkRequest request;
        request.setUrl(QUrl(m_baseUrl + "/BeoNotify/Notifications"));

        QNetworkReply *reply = m_manager->get(request);

        // read the streaming json
        QObject::connect(reply, &QIODevice::readyRead, this, [=]() {
            if (!reply->error()) {
                setState(CONNECTED);
                QString     answer  = reply->readAll();
                QStringList answers = answer.split("\r\n\r\n");

                for (int i = 0; i < answers.length(); i++) {
                    QString answerSingle = answers[i].trimmed();

                    QVariantMap map;
                    if (answerSingle != "") {
                        // convert to json
                        QJsonParseError parseerror;
                        QJsonDocument   doc = QJsonDocument::fromJson(answerSingle.toUtf8(), &parseerror);
                        if (parseerror.error != QJsonParseError::NoError) {
                            qCWarning(m_logCategory) << "JSON error : " << parseerror.errorString();
                            return;
                        }

                        // createa a map object and update entity
                        map = doc.toVariant().toMap().value("notification").toMap();
                        updateEntity(m_entityId, map);
                    }
                }
            } else {
                qCDebug(m_logCategory) << "Cannot connect" << reply->errorString();
                if (m_state != DISCONNECTED) {
                    setState(DISCONNECTED);
                }
            }
        });

        // handle closed connection
        QObject::connect(m_manager, &QNetworkAccessManager::finished, this, [=]() {
            qCDebug(m_logCategory) << "Network access manager finished";
            if (m_manager) {
                m_manager->deleteLater();
                qCDebug(m_logCategory) << "Network access manager deleted.";
            }
        });

        // handle dropped connection
        QObject::connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error), this,
                         [=](QNetworkReply::NetworkError code) {
                             if (!m_userDisconnect) {
                                 qCDebug(m_logCategory) << "Bang & Olufsen product disconnected" << code;
                                 if (m_state != DISCONNECTED) {
                                     setState(DISCONNECTED);
                                 }
                             }
                         });

        QObject::connect(this, &BangOlufsen::disconnected, this, [=]() {
            qCDebug(m_logCategory) << "Disconnected";
            if (m_manager) {
                m_manager->deleteLater();
                qCDebug(m_logCategory) << "Network access manager deleted.";
            }
        });
    }
}

void BangOlufsen::disconnect() {
    if (m_pollingTimer->isActive()) {
        m_pollingTimer->stop();
        qCDebug(m_logCategory) << "Polling timer stopped.";
    }
    if (m_state != DISCONNECTED) {
        qCDebug(m_logCategory) << "Disconnecting a Bang & Olufsen product";
        m_userDisconnect = true;
        setState(DISCONNECTED);
    }
}

void BangOlufsen::enterStandby() { disconnect(); }

void BangOlufsen::leaveStandby() { connect(); }

void BangOlufsen::sendCommand(const QString &type, const QString &entity_id, int command, const QVariant &param) {
    if (entity_id == m_entityId && type == "media_player") {
        if (command == MediaPlayerDef::C_VOLUME_SET) {
            setVolume(param.toInt());
        } else if (command == MediaPlayerDef::C_PLAY) {
            Play();
        } else if (command == MediaPlayerDef::C_MUTE) {
            EntityInterface *     entity = m_entities->getEntityInterface(entity_id);
            MediaPlayerInterface *me     = static_cast<MediaPlayerInterface *>(entity->getSpecificInterface());
            if (entity) {
                setMute(!me->muted());
            }
        } else if (command == MediaPlayerDef::C_PAUSE) {
            Pause();
        } else if (command == MediaPlayerDef::C_PREVIOUS) {
            Prev();
        } else if (command == MediaPlayerDef::C_NEXT) {
            Next();
        } else if (command == MediaPlayerDef::C_TURNON) {
            TurnOn();
        } else if (command == MediaPlayerDef::C_TURNOFF) {
            Standby();
        }
    }
}

void BangOlufsen::getRequest(const QString &url) {
    // create new networkacces manager and request
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject *context = new QObject(this);

    // connect to finish signal
    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply *reply) {
        if (reply->error()) {
            qCWarning(m_logCategory) << reply->errorString();
        }

        QString     answer = reply->readAll();
        QVariantMap map;
        if (answer != "") {
            // convert to json
            QJsonParseError parseerror;
            QJsonDocument   doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
            if (parseerror.error != QJsonParseError::NoError) {
                qCWarning(m_logCategory) << "JSON error : " << parseerror.errorString();
                return;
            }

            // createa a map object
            map = doc.toVariant().toMap();
            emit requestReady(map, url);
        }

        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    // set the URL
    // url = "/BeoDevice/powerManagement/standby"
    request.setUrl(QUrl(m_baseUrl + url));

    // send the get request
    manager->get(request);
}

void BangOlufsen::postRequest(const QString &url, const QString &params) {
    // create new networkacces manager and request
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject *context = new QObject(this);

    // connect to finish signal
    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply *reply) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCDebug(m_logCategory) << "POST REQUEST " << statusCode;
        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    request.setUrl(QUrl(m_baseUrl + url + params));

    // send the get request
    manager->post(request, "");
}

void BangOlufsen::putRequest(const QString &url, const QVariantMap &params) {
    // create new networkacces manager and request
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject *context = new QObject(this);

    // connect to finish signal
    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply *reply) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCDebug(m_logCategory) << "PUT REQUEST " << statusCode << reply->readAll();
        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    request.setUrl(QUrl(m_baseUrl + url));

    QJsonDocument json       = QJsonDocument::fromVariant(params);
    QString       jsonString = json.toJson(QJsonDocument::JsonFormat::Compact);
    QByteArray    data       = jsonString.toUtf8();

    // send the get request
    manager->put(request, data);
}

void BangOlufsen::putRequest(const QString &url) { putRequest(url, QVariantMap()); }

int BangOlufsen::getVolume(const QVariantMap &map) {
    int volume = -1;
    if (map.contains("data") && map.value("type").toString() == "VOLUME") {
        volume = static_cast<int>(map.value("data").toMap().value("speaker").toMap().value("level").toDouble());
    }
    return volume;
}

bool BangOlufsen::getMuted(const QVariantMap &map) {
    bool muted = false;
    if (map.contains("data") && map.value("type").toString() == "VOLUME") {
        muted = map.value("data").toMap().value("speaker").toMap().value("muted").toBool();
    }
    return muted;
}

QString BangOlufsen::getSource(const QVariantMap &map) {
    QString source;
    if (map.contains("data") && map.value("type").toString() == "SOURCE") {
        source = map.value("data")
                     .toMap()
                     .value("primaryExperience")
                     .toMap()
                     .value("source")
                     .toMap()
                     .value("friendlyName")
                     .toString();
    }
    return source;
}

QString BangOlufsen::getState(const QVariantMap &map) {
    QString state;
    if (map.contains("data") && map.value("type").toString() == "PROGRESS_INFORMATION") {
        state = map.value("data").toMap().value("state").toString();
    }
    return state;
}

void BangOlufsen::getStandby() {
    QString url = "/BeoDevice/powerManagement/standby";

    QObject *context = new QObject(this);

    QObject::connect(this, &BangOlufsen::requestReady, context, [=](const QVariantMap &map, const QString &rUrl) {
        if (rUrl == url) {
            EntityInterface *entity = m_entities->getEntityInterface(m_entityId);

            if (entity && entity->isSupported(MediaPlayerDef::F_TURN_ON) &&
                entity->isSupported(MediaPlayerDef::F_TURN_OFF)) {
                if (map.contains("standby") && map.value("standby").toMap().value("powerState").toString() == "on") {
                    entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::ON);
                } else {
                    entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::OFF);
                }
            }
        }
        context->deleteLater();
    });

    getRequest(url);
}

QVariantMap BangOlufsen::getMusicInfo(const QVariantMap &map) {
    QVariantMap musicInfo;
    if (map.value("data").toMap().contains("trackImage") &&
        map.value("type").toString() == "NOW_PLAYING_STORED_MUSIC") {
        if (map.value("data").toMap().contains("trackImage") &&
            map.value("data").toMap().value("trackImage").toList().length() > 0) {
            musicInfo.insert("mediaUrl",
                             map.value("data").toMap().value("trackImage").toList()[0].toMap().value("url").toString());
        }
        musicInfo.insert("mediaArtist", map.value("data").toMap().value("artist").toString());
        musicInfo.insert("mediaTrack", map.value("data").toMap().value("name").toString());
        musicInfo.insert("mediaAlbum", map.value("data").toMap().value("album").toString());
    }

    if (map.value("type").toString() == "NOW_PLAYING_NET_RADIO") {
        if (map.value("data").toMap().contains("image") &&
            map.value("data").toMap().value("image").toList().length() > 0) {
            musicInfo.insert("mediaUrl",
                             map.value("data").toMap().value("image").toList()[0].toMap().value("url").toString());
        }
        musicInfo.insert("mediaArtist", map.value("data").toMap().value("name").toString());
        musicInfo.insert("mediaTrack", map.value("data").toMap().value("liveDescription").toString());
        musicInfo.insert("mediaAlbum", "");
    }

    return musicInfo;
}

int BangOlufsen::getPosition(const QVariantMap &map) {
    int position = 0;
    if (map.contains("data") && map.value("type").toString() == "PROGRESS_INFORMATION") {
        position = map.value("data").toMap().value("position").toInt();
    }
    return position;
}

int BangOlufsen::getDuration(const QVariantMap &map) {
    int duration = 0;
    if (map.contains("data") && map.value("type").toString() == "PROGRESS_INFORMATION") {
        duration = map.value("data").toMap().value("totalDuration").toInt();
    }
    return duration;
}

void BangOlufsen::setVolume(const int &volume) {
    QVariantMap data;
    data.insert("level", volume);
    putRequest("/BeoZone/Zone/Sound/Volume/Speaker/Level", data);
}

void BangOlufsen::setMute(const bool &value) {
    QVariantMap data;
    data.insert("muted", value);
    putRequest("/BeoZone/Zone/Sound/Volume/Speaker/Muted", data);
}

void BangOlufsen::Play() {
    postRequest("/BeoZone/Zone/Stream/Play", "");
    postRequest("/BeoZone/Zone/Stream/Play/Release", "");
}

void BangOlufsen::Pause() {
    postRequest("/BeoZone/Zone/Stream/Pause", "");
    postRequest("/BeoZone/Zone/Stream/Pause/Release", "");
}

void BangOlufsen::Stop() {
    postRequest("/BeoZone/Zone/Stream/Stop", "");
    postRequest("/BeoZone/Zone/Stream/Stop/Release", "");
}

void BangOlufsen::Next() {
    postRequest("/BeoZone/Zone/Stream/Forward", "");
    postRequest("/BeoZone/Zone/Stream/Forward/Release", "");
}

void BangOlufsen::Prev() {
    postRequest("/BeoZone/Zone/Stream/Backward", "");
    postRequest("/BeoZone/Zone/Stream/Backward/Release", "");
}

void BangOlufsen::Standby() {
    QVariantMap data;
    data.insert("powerState", "standby");
    QVariantMap motherData;
    motherData.insert("standby", data);
    putRequest("/BeoDevice/powerManagement/standby", motherData);
}

void BangOlufsen::TurnOn() {
    QVariantMap data;
    data.insert("powerState", "on");
    QVariantMap motherData;
    motherData.insert("standby", data);
    putRequest("/BeoDevice/powerManagement/standby", motherData);
}

void BangOlufsen::onPollingTimerTimeout() { getStandby(); }
