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

#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

#include "yio-plugin/integration.h"
#include "yio-plugin/plugin.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// BANG&OLUFSEN FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const bool USE_WORKER_THREAD = false;

class BangOlufsenPlugin : public Plugin {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "bangolufsen.json")

 public:
    BangOlufsenPlugin();

    // Plugin interface
 protected:
    Integration* createIntegration(const QVariantMap& config, EntitiesInterface* entities,
                                   NotificationsInterface* notifications, YioAPIInterface* api,
                                   ConfigInterface* configObj) override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// BANG&OLUFSEN CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class BangOlufsen : public Integration {
    Q_OBJECT

 public:
    BangOlufsen(const QVariantMap& config, EntitiesInterface* entities, NotificationsInterface* notifications,
                YioAPIInterface* api, ConfigInterface* configObj, Plugin* plugin);

    void sendCommand(const QString& type, const QString& entityId, int command, const QVariant& param) override;

 signals:
    void requestReady(const QVariantMap& obj, const QString& url);

 public slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void connect() override;
    void disconnect() override;
    void enterStandby() override;
    void leaveStandby() override;

 private:
    void updateEntity(const QString& entity_id, const QVariantMap& map);

    // get, post and put requests
    QNetworkReply* getRequest(const QString& url);
    void           postRequest(const QString& url, const QString& params);
    void           putRequest(const QString& url, const QVariantMap& params);
    void           putRequest(const QString& url);

    QString m_ip;
    QString m_baseUrl;
    QString m_entityId;

    QNetworkAccessManager* m_manager;

    bool m_userDisconnect = false;

    // polling
    QTimer* m_pollingTimer;

    //    // get information from the speaker
    int     getVolume(const QVariantMap& map);
    bool    getMuted(const QVariantMap& map);
    QString getSource(const QVariantMap& map);
    //    void getSources();  // poll
    QString     getState(const QVariantMap& map);
    void        getStandby();  // poll
    QVariantMap getMusicInfo(const QVariantMap& map);
    int         getPosition(const QVariantMap& map);
    int         getDuration(const QVariantMap& map);
    //    void getPrimariyExperience();

    //    // commands to the speaker
    void setVolume(const int& volume);
    void setMute(const bool& value);
    void Play();
    void Pause();
    void Stop();
    void Next();
    void Prev();
    void Standby();
    void TurnOn();
    //    void setSource(const QVariantMap& source);
    //    void joinExperience();
    //    void leaveExperience();

 private slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void onPollingTimerTimeout();
};
