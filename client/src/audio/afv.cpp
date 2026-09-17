/*
 * xPilot: X-Plane pilot client for VATSIM
 * Copyright (C) 2019-2024 Justin Shannon
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
*/

#include "afv.h"

#include <cmath>
#include "config/appconfig.h"
#include "common/utils.h"
#include "common/build_config.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

using namespace afv_native::afv;

namespace xpilot
{
    float ScaleVolume(float v)
    {
        return 1.0f - sqrt(1.0f - (v * v));
    }

    static void defaultLogger(const char *subsystem, const char *file, int line, const char *outputLine, void* ref)
    {
        auto *self = reinterpret_cast<AudioForVatsim *>(ref);
        self->afvLogger(QString("%1: %2: %3\r\n")
                            .arg(QDateTime::currentDateTimeUtc().toString("MMM dd HH:mm:ss yyyy"))
                            .arg(QString::fromUtf8(subsystem).leftJustified(20, ' '))
                            .arg(outputLine));
    }

    static afv_native::log_fn gLogger = defaultLogger;

    AudioForVatsim::AudioForVatsim(QObject* parent) :
        QObject(parent),
        m_xplaneAdapter(*QInjection::Pointer<XplaneAdapter>().data()),
        m_networkManager(*QInjection::Pointer<NetworkManager>().data()),
        m_controllerManager(*QInjection::Pointer<ControllerManager>().data()),
        m_client()
    {
        QDir afvLogPath(pathAppend(AppConfig::getInstance()->dataRoot(), "AfvLogs"));
        if(!afvLogPath.exists()) {
            afvLogPath.mkpath(".");
        }

        m_afvLog.setFileName(pathAppend(afvLogPath.path(), QString("AfvLog-%1.txt").arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss"))));
        if(m_afvLog.open(QFile::WriteOnly))
        {
            m_logDataStream.setDevice(&m_afvLog);
        }

        // keep only the last 10 log files
        QFileInfoList files = afvLogPath.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
        const int MAX_LOGS_TO_RETAIN = 10;
        for(int index = files.size(); index > MAX_LOGS_TO_RETAIN; --index) {
            const QFileInfo &info = files.at(index - 1);
            QFile::remove(info.absoluteFilePath());
        }

        m_transceiverTimer.setInterval(5000);
        m_rxTxQueryTimer.setInterval(50);
        m_vuTimer.setInterval(10);
        m_vuTimer.start();
        m_audioDevicesTimer.setInterval(500);
        m_configSaveTimer.setSingleShot(true);
        m_configSaveTimer.setInterval(250);
        connect(&m_configSaveTimer, &QTimer::timeout, this, [] {
            AppConfig::getInstance()->saveConfig();
        });

#ifdef Q_OS_WIN
        WORD wVersionRequested;
        WSADATA wsaData;
        wVersionRequested = MAKEWORD(2, 2);
        WSAStartup(wVersionRequested, &wsaData);
#endif

        QString clientName = QString("xPilot %1").arg(BuildConfig::getVersionString());

        m_eventBase.reset(event_base_new());
        if(!m_eventBase) {
            qFatal("Could not allocate the AFV event base.");
        }
        m_client = std::make_shared<afv_native::Client>(m_eventBase.get(), 2, clientName.toStdString());
        afv_native::setLogger(gLogger, this);
        m_client->ClientEventCallback.addCallback(this, [this](afv_native::ClientEventType evt, void* data, void*)
        {
            if(m_shuttingDown.load()) return;
            switch(evt)
            {
                case afv_native::ClientEventType::APIServerError:
                    if(data != nullptr) {
                        auto error = *reinterpret_cast<APISessionError*>(data);
                        switch(error) {
                            case APISessionError::BadPassword:
                            case APISessionError::RejectedCredentials:
                                emit notificationPosted("Error connecting to voice server. Please check your VATSIM credentials and try again.", MessageType::Error);
                                break;
                            case APISessionError::ConnectionError:
                                emit notificationPosted("Error initiating voice server connection.", MessageType::Error);
                                break;
                            case APISessionError::AuthTokenExpiryTimeInPast:
                                emit notificationPosted("Voice server auth token expiry time is in the past. Please make sure your system clock is synchronized.", MessageType::Error);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case afv_native::ClientEventType::VoiceServerChannelError:
                    if(data != nullptr) {
                        int error = *reinterpret_cast<int*>(data);
                        emit notificationPosted(QString("Voice server error: %1").arg(error), MessageType::Error);
                    }
                    break;
                case afv_native::ClientEventType::VoiceServerError:
                    if(data != nullptr) {
                        auto error = *reinterpret_cast<VoiceSessionError*>(data);
                        switch(error) {
                            case VoiceSessionError::BadResponseFromAPIServer:
                                emit notificationPosted("Voice server error: BadResponseFromAPIServer", MessageType::Error);
                                break;
                            case VoiceSessionError::Timeout:
                                emit notificationPosted("Voice server error: Timeout", MessageType::Error);
                                break;
                            case VoiceSessionError::UDPChannelError:
                                emit notificationPosted("Voice server error: UDPChannelError", MessageType::Error);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case afv_native::ClientEventType::StationAliasesUpdated:
                    {
                        const auto stations = m_client->getStationAliases();
                        const QVector<afv_native::afv::dto::Station> aliases(stations.begin(), stations.end());
                        QMetaObject::invokeMethod(this, [this, aliases] {
                            if(!m_shuttingDown.load()) {
                                m_aliasedStations = aliases;
                            }
                        }, Qt::QueuedConnection);
                    }
                    break;
                case afv_native::ClientEventType::VoiceServerConnected:
                    emit notificationPosted("Connected to voice server.", MessageType::Info);
                    break;
                case afv_native::ClientEventType::VoiceServerDisconnected:
                    emit notificationPosted("Disconnected from voice server.", MessageType::Info);
                    break;
                case afv_native::ClientEventType::AudioError:
                    if(data != nullptr) {
                        const QString error = QString::fromUtf8(static_cast<const char*>(data));
                        QMetaObject::invokeMethod(this, [this, error]() {
                            if(!m_shuttingDown.load()) emit notificationPosted(error, MessageType::Error);
                        }, Qt::QueuedConnection);
                    }
                    break;
                default:
                    break;
            }
        });
        m_client->setEnableInputFilters(true);
        m_client->setEnableOutputEffects(!AppConfig::getInstance()->AudioEffectsDisabled);
        m_client->setEnableHfSquelch(AppConfig::getInstance()->HFSquelchEnabled);
        m_client->setAutoOutputGain(AppConfig::getInstance()->AutoOutputVolumeBalance);
        m_client->setAutoOutputGainStrength(AppConfig::getInstance()->AutoOutputVolumeBalanceStrength / 100.0f);

        configureAudioDevices();
        setMicrophoneVolume(AppConfig::getInstance()->MicrophoneVolume);
        setCom1Volume(AppConfig::getInstance()->Com1Volume);
        setCom2Volume(AppConfig::getInstance()->Com2Volume);

        connect(&m_audioDevicesTimer, &QTimer::timeout, this, &AudioForVatsim::OnAudioDevicesTimer);
        connect(&m_transceiverTimer, &QTimer::timeout, this, &AudioForVatsim::OnTransceiverTimer);
        connect(&m_rxTxQueryTimer, &QTimer::timeout, this, [&]{
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
            const bool powered = m_radioStackState.AvionicsPowerOn || m_radioStackState.OverrideRadioPower;
            const bool com1Rx = powered && m_radioStackState.Com1ReceiveEnabled && m_client->getRxActive(0);
            const bool com2Rx = powered && m_radioStackState.Com2ReceiveEnabled && m_client->getRxActive(1);

            emit radioRxChanged(0, com1Rx);
            emit radioRxChanged(1, com2Rx);

            m_xplaneAdapter.setComRxDataref(0, com1Rx);
            m_xplaneAdapter.setComRxDataref(1, com2Rx);
        });
        connect(&m_vuTimer, &QTimer::timeout, this, [=]{
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
            double vu = m_client->getInputPeak();
            emit inputVuChanged(vu);
            m_xplaneAdapter.setVuDataref(vu);
        });
        connect(&m_networkManager, &NetworkManager::networkConnected, this, &AudioForVatsim::OnNetworkConnected);
        connect(&m_networkManager, &NetworkManager::networkDisconnected, this, &AudioForVatsim::OnNetworkDisconnected);
        connect(&m_networkManager, &NetworkManager::disableVoiceTransmit, this, [&] {
            DisableVoiceTransmit();
        });
        connect(&m_networkManager, &NetworkManager::muteReceived, this, [&](bool mute) {
            if(mute) {
                DisableVoiceTransmit();
            } else {
                EnableVoiceTransmit();
            }
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::radioStackStateChanged, this, [&](RadioStackState state){
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
            if(state != m_radioStackState) {
                m_radioStackState = state;

                if(m_radioStackState.Com1TransmitEnabled) {
                    m_client->setTxRadio(0);
                }
                else if(m_radioStackState.Com2TransmitEnabled) {
                    m_client->setTxRadio(1);
                }

                if(AppConfig::getInstance()->AircraftRadioStackControlsVolume) {
                    setCom1Volume(m_radioStackState.Com1Volume);
                    setCom2Volume(m_radioStackState.Com2Volume);
                }

                updateTransceivers();
            }
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::userAircraftDataChanged, this, [&](UserAircraftData data){
            if(data != m_userAircraftData) {
                m_userAircraftData = data;
            }
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::pttPressed, this, [&]{
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
            if(m_voiceTransmitDisabled) {
                m_client->setPtt(false);
            }
            else {
                m_client->setPtt(true);
            }
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::pttReleased, this, [&]{
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
            m_client->setPtt(false);
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::com1OnHeadsetChanged, this, [&](bool onHeadset) {
            setOnHeadset(0, onHeadset);
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::com2OnHeadsetChanged, this, [&](bool onHeadset) {
            setOnHeadset(1, onHeadset);
        });
        connect(&m_xplaneAdapter, &XplaneAdapter::splitAudioChannelsChanged, this, [&](bool split) {
            setSplitAudioChannels(split);
        });

        connect(&m_controllerManager, &ControllerManager::controllerAdded, this, [&](Controller controller)
        {
            m_controllers.push_back(controller);
        });
        connect(&m_controllerManager, &ControllerManager::controllerUpdated, this, [&](Controller controller)
        {
            auto it = std::find_if(m_controllers.begin(), m_controllers.end(), [=](Controller &c)
            {
                return c.Callsign == controller.Callsign;
            });

            if(it != m_controllers.constEnd())
            {
                *it = controller;
            }
        });
        connect(&m_controllerManager, &ControllerManager::controllerDeleted, this, [&](Controller controller)
        {
            auto it = std::find_if(m_controllers.begin(), m_controllers.end(), [=](Controller &c)
            {
                return c.Callsign == controller.Callsign;
            });
            if(it != m_controllers.end())
            {
                m_controllers.removeAll(*it);
            }
        });
        connect(this, &AudioForVatsim::notificationPosted, this, [&](QString message, MessageType type)
        {
            m_xplaneAdapter.NotificationPosted(message, toColorHex(type));
        });

        m_workerThread.reset(QThread::create([this] {
            while(!QThread::currentThread()->isInterruptionRequested())
            {
                {
                    std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
                    event_base_loop(m_eventBase.get(), EVLOOP_NONBLOCK);
                }
                QThread::msleep(10);
            }
        }));
        m_workerThread->start();
    }

    AudioForVatsim::~AudioForVatsim()
    {
        m_shuttingDown.store(true);
        m_transceiverTimer.stop();
        m_rxTxQueryTimer.stop();
        m_vuTimer.stop();
        m_audioDevicesTimer.stop();
        flushConfigSave();
        // Finish libevent callbacks before releasing any of their owners.
        m_workerThread->requestInterruption();
        m_workerThread->wait();
        m_workerThread.reset();
        m_client->stopAudio();
        m_client->ClientEventCallback.removeCallback(this);
        m_client->disconnect();
        m_client.reset();
        // setLogger must synchronize with in-flight native logger callbacks.
        afv_native::setLogger(nullptr, nullptr);
        m_eventBase.reset();
#ifdef Q_OS_WIN
        WSACleanup();
#endif
    }

    void AudioForVatsim::afvLogger(QString message)
    {
        if(m_shuttingDown.load()) return;
        // QFile/QTextStream belong to the QObject thread. Copy the message before
        // returning to the native logger; queued calls are discarded on destruction.
        QMetaObject::invokeMethod(this, [this, message] {
            if(!m_shuttingDown.load() && m_afvLog.isOpen()) {
                m_logDataStream << message;
                m_logDataStream.flush();
            }
        }, Qt::QueuedConnection);
    }

    void AudioForVatsim::scheduleConfigSave()
    {
        if(!m_settingsOpen) m_configSaveTimer.start();
    }

    void AudioForVatsim::flushConfigSave()
    {
        if(m_configSaveTimer.isActive()) {
            m_configSaveTimer.stop();
            AppConfig::getInstance()->saveConfig();
        }
    }

    void AudioForVatsim::setInputDevice(QString deviceName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        if(deviceName.isEmpty() || (deviceName == m_inputDevice && m_client->getMicrophoneDevice())) return;
        const auto device = std::find_if(m_inputDevices.cbegin(), m_inputDevices.cend(),
                                        [&](const AudioDeviceInfo &entry) { return entry.DeviceName == deviceName; });
        if(device == m_inputDevices.cend()) return;
        m_client->setMicrophoneDevice(deviceName.toStdString());
        m_client->startMicrophone();
        m_inputDevice = deviceName;
    }

    void AudioForVatsim::setSpeakerDevice(QString deviceName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        if(deviceName.isEmpty() || (deviceName == m_speakerDevice && m_client->getSpeakerDevice())) return;
        const auto device = std::find_if(m_outputDevices.cbegin(), m_outputDevices.cend(),
                                        [&](const AudioDeviceInfo &entry) { return entry.DeviceName == deviceName; });
        if(device == m_outputDevices.cend()) return;
        m_client->setSpeakerDevice(deviceName.toStdString());
        m_client->startSpeaker();
        m_speakerDevice = deviceName;
    }

    void AudioForVatsim::setHeadsetDevice(QString deviceName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        if(deviceName.isEmpty() || (deviceName == m_headsetDevice && m_client->getHeadsetDevice())) return;
        const auto device = std::find_if(m_outputDevices.cbegin(), m_outputDevices.cend(),
                                        [&](const AudioDeviceInfo &entry) { return entry.DeviceName == deviceName; });
        if(device == m_outputDevices.cend()) return;
        m_client->setHeadsetDevice(deviceName.toStdString());
        m_client->startHeadset();
        m_headsetDevice = deviceName;
    }

    void AudioForVatsim::setCom1Volume(double volume)
    {
        if(!std::isfinite(volume)) return;
        const int value = qRound(qBound(0.0, volume, 100.0));
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        m_com1BaseVolume = value;
        updateRadioGain(0);
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->Com1Volume != value) {
            config->Com1Volume = value;
            config->setCom1Volume(value);
            scheduleConfigSave();
        }
    }

    void AudioForVatsim::setCom2Volume(double volume)
    {
        if(!std::isfinite(volume)) return;
        const int value = qRound(qBound(0.0, volume, 100.0));
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        m_com2BaseVolume = value;
        updateRadioGain(1);
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->Com2Volume != value) {
            config->Com2Volume = value;
            config->setCom2Volume(value);
            scheduleConfigSave();
        }
    }

    void AudioForVatsim::setAutoOutputVolumeBalance(bool enabled)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        const auto value = enabled;
        m_client->setAutoOutputGain(value);
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->AutoOutputVolumeBalance != value) {
            config->AutoOutputVolumeBalance = value;
            config->setAutoOutputVolumeBalance(value);
            scheduleConfigSave();
        }
    }

    void AudioForVatsim::setAutoOutputVolumeBalanceStrength(int strength)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        const auto value = qBound(0, strength, 100);
        m_client->setAutoOutputGainStrength(value / 100.0f);
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->AutoOutputVolumeBalanceStrength != value) {
            config->AutoOutputVolumeBalanceStrength = value;
            config->setAutoOutputVolumeBalanceStrength(value);
            scheduleConfigSave();
        }
    }

    void AudioForVatsim::disableAudioEffects(bool disabled)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        const auto value = disabled;
        m_client->setEnableOutputEffects(!value);
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->AudioEffectsDisabled != value) {
            config->AudioEffectsDisabled = value;
            config->setAudioEffectsDisabled(value);
            scheduleConfigSave();
        }
    }

    void AudioForVatsim::enableHfSquelch(bool enabled)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        const auto value = enabled;
        m_client->setEnableHfSquelch(value);
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->HFSquelchEnabled != value) {
            config->HFSquelchEnabled = value;
            config->setHFSquelchEnabled(value);
            scheduleConfigSave();
        }
    }

    void AudioForVatsim::OnNetworkConnected(QString callsign, bool enableVoice)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        if(!enableVoice)
            return;

        m_client->setCallsign(callsign.toStdString());
        m_client->setCredentials(AppConfig::getInstance()->VatsimId.toStdString(), AppConfig::getInstance()->VatsimPasswordDecrypted.toStdString());
        m_client->connect();
        m_transceiverTimer.start();
        m_rxTxQueryTimer.start();
        m_xplaneAdapter.EnableVoiceTransmit();
    }

    void AudioForVatsim::OnNetworkDisconnected()
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        emit radioRxChanged(0, false);
        emit radioRxChanged(1, false);
        m_xplaneAdapter.setComRxDataref(0, false);
        m_xplaneAdapter.setComRxDataref(1, false);

        m_client->disconnect();
        m_transceiverTimer.stop();
        m_rxTxQueryTimer.stop();
        m_xplaneAdapter.EnableVoiceTransmit();
    }

    void AudioForVatsim::OnTransceiverTimer()
    {
        updateTransceivers();
    }

    void AudioForVatsim::OnAudioDevicesTimer()
    {
        // output devices
        QList<AudioDeviceInfo> newOutputDevices;
        for(const auto& device: afv_native::audio::AudioDevice::getCompatibleOutputDevicesForApi(0))
        {
            AudioDeviceInfo audioDevice{};
            audioDevice.DeviceName = device.second.name.c_str();
            audioDevice.Id = (QChar)device.first;
            newOutputDevices.append(audioDevice);
        }

        bool outputDeviceListChanged = !std::equal(std::begin(m_outputDevices), std::end(m_outputDevices),
                                                std::begin(newOutputDevices), std::end(newOutputDevices));
        if(outputDeviceListChanged) {
            m_outputDevices.clear();
            m_outputDevices = newOutputDevices;
            emit outputDevicesChanged();
        }

        // input devices
        QList<AudioDeviceInfo> newInputDevices;
        for(const auto& device: afv_native::audio::AudioDevice::getCompatibleInputDevicesForApi(0))
        {
            AudioDeviceInfo audioDevice{};
            audioDevice.DeviceName = device.second.name.c_str();
            audioDevice.Id = (QChar)device.first;
            newInputDevices.append(audioDevice);
        }

        bool inputDeviceListChanged = !std::equal(std::begin(m_inputDevices), std::end(m_inputDevices),
                                                std::begin(newInputDevices), std::end(newInputDevices));
        if(inputDeviceListChanged) {
            m_inputDevices.clear();
            m_inputDevices = newInputDevices;
            emit inputDevicesChanged();
        }
    }

    void AudioForVatsim::configureAudioDevices()
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        m_client->stopAudio();

        m_outputDevices.clear();
        m_inputDevices.clear();

        auto outputDevices = afv_native::audio::AudioDevice::getCompatibleOutputDevicesForApi(0);
        for(const auto& device: outputDevices)
        {
            AudioDeviceInfo audioDevice{};
            audioDevice.DeviceName = device.second.name.c_str();
            audioDevice.Id = (QChar)device.first;
            m_outputDevices.append(audioDevice);
        }

        emit outputDevicesChanged();

        auto inputDevices = afv_native::audio::AudioDevice::getCompatibleInputDevicesForApi(0);
        for(const auto& device: inputDevices)
        {
            AudioDeviceInfo audioDevice{};
            audioDevice.DeviceName = device.second.name.c_str();
            audioDevice.Id = (QChar)device.first;
            m_inputDevices.append(audioDevice);
        }

        emit inputDevicesChanged();

        // Empty committed names must also replace any temporary device preview.
        m_client->setMicrophoneDevice(AppConfig::getInstance()->InputDevice.toStdString());
        m_client->setSpeakerDevice(AppConfig::getInstance()->SpeakerDevice.toStdString());
        m_client->setHeadsetDevice(AppConfig::getInstance()->HeadsetDevice.toStdString());

        m_splitAudioChannels = AppConfig::getInstance()->SplitAudioChannels;
        m_client->setSplitAudioChannels(m_splitAudioChannels);
        m_xplaneAdapter.setSplitAudioChannels(m_splitAudioChannels);
        setOnHeadset(0, AppConfig::getInstance()->Com1OnHeadset);
        setOnHeadset(1, AppConfig::getInstance()->Com2OnHeadset);

        m_client->startAudio();
        m_inputDevice = AppConfig::getInstance()->InputDevice;
        m_headsetDevice = AppConfig::getInstance()->HeadsetDevice;
        m_speakerDevice = AppConfig::getInstance()->SpeakerDevice;
    }

    void AudioForVatsim::updateTransceivers()
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        quint32 com1Alias = getAliasFrequency(m_radioStackState.Com1Frequency * 1000);
        quint32 com2Alias = getAliasFrequency(m_radioStackState.Com2Frequency * 1000);

        com1Alias > 0 ? (emit radioAliasChanged(0, com1Alias)) : (emit radioAliasChanged(0, 0));
        com2Alias > 0 ? (emit radioAliasChanged(1, com2Alias)) : (emit radioAliasChanged(1, 0));

        m_client->setRadioState(0, m_radioStackState.Com1ReceiveEnabled && (m_radioStackState.AvionicsPowerOn || m_radioStackState.OverrideRadioPower)
                                       ? (com1Alias > 0 ? com1Alias : m_radioStackState.Com1Frequency * 1000) : 0);
        m_client->setRadioState(1, m_radioStackState.Com2ReceiveEnabled && (m_radioStackState.AvionicsPowerOn || m_radioStackState.OverrideRadioPower)
                                       ? (com2Alias > 0 ? com2Alias : m_radioStackState.Com2Frequency * 1000) : 0);
        m_client->setClientPosition(m_userAircraftData.Latitude, m_userAircraftData.Longitude, m_userAircraftData.AltitudeMslM,
                                    m_userAircraftData.AltitudeAglM);
    }

    void AudioForVatsim::EnableVoiceTransmit()
    {
        m_voiceTransmitDisabled = false;
        m_xplaneAdapter.EnableVoiceTransmit();
    }

    void AudioForVatsim::DisableVoiceTransmit()
    {
        m_voiceTransmitDisabled = true;
        m_xplaneAdapter.DisableVoiceTransmit();
    }

    void AudioForVatsim::setMicrophoneVolume(int volume)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        m_client->setMicrophoneVolume(qBound(-60, volume, 18));
    }

    void AudioForVatsim::setOnHeadset(unsigned int radio, bool onHeadset)
    {
        if(radio >= 2) return;
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        const bool changed = (radio == 0 ? AppConfig::getInstance()->Com1OnHeadset : AppConfig::getInstance()->Com2OnHeadset) != onHeadset;
        m_client->setOnHeadset(radio, onHeadset);

        if(radio == 0) {
            AppConfig::getInstance()->Com1OnHeadset = onHeadset;
            m_xplaneAdapter.setCom1OnHeadset(onHeadset);
        }
        else {
            AppConfig::getInstance()->Com2OnHeadset = onHeadset;
            m_xplaneAdapter.setCom2OnHeadset(onHeadset);
        }

        if(changed) scheduleConfigSave();
    }

    void AudioForVatsim::setSplitAudioChannels(bool split)
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
        if(m_splitAudioChannels != split) {
            m_client->stopAudio();
            m_client->setSplitAudioChannels(split);
            m_client->startAudio();
            m_splitAudioChannels = split;
        }
        auto *config = AppConfig::getInstance();
        if(!m_settingsOpen && config->SplitAudioChannels != split) {
            config->SplitAudioChannels = split;
            config->setSplitAudioChannels(split);
            scheduleConfigSave();
        }
        m_xplaneAdapter.setSplitAudioChannels(split);
    }

    void AudioForVatsim::updateRadioGain(unsigned int radio)
    {
        m_client->setRadioGain(radio, getBaseRadioGain(radio));
    }

    float AudioForVatsim::getBaseRadioGain(unsigned int radio) const
    {
        double volume = radio == 0 ? m_com1BaseVolume : m_com2BaseVolume;
        return ScaleVolume(qBound(0.0, volume, 100.0) / 100.0f);
    }

    void AudioForVatsim::settingsWindowOpened()
    {
        if(m_settingsOpen) return;
        flushConfigSave();
        AppConfig::getInstance()->setInitialTempValues();
        m_settingsOpen = true;
        OnAudioDevicesTimer();
        // Disconnect and device failures release the native device. Calibration
        // needs a live microphone even if the selected name has not changed.
        {
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);
            if(!m_client->getMicrophoneDevice()) {
                setInputDevice(AppConfig::getInstance()->InputDevice);
            }
        }
        m_audioDevicesTimer.start();
    }

    void AudioForVatsim::settingsWindowClosed()
    {
        if(!m_settingsOpen) return;
        m_audioDevicesTimer.stop();
        // QML previews are temporary. Apply/OK copy the staged values into the
        // committed config; Cancel leaves it unchanged. Restore that baseline.
        auto *config = AppConfig::getInstance();
        if(m_inputDevice != config->InputDevice || m_headsetDevice != config->HeadsetDevice ||
           m_speakerDevice != config->SpeakerDevice || m_splitAudioChannels != config->SplitAudioChannels) {
            configureAudioDevices();
        }
        setCom1Volume(config->Com1Volume);
        setCom2Volume(config->Com2Volume);
        setMicrophoneVolume(config->MicrophoneVolume);
        disableAudioEffects(config->AudioEffectsDisabled);
        enableHfSquelch(config->HFSquelchEnabled);
        setAutoOutputVolumeBalance(config->AutoOutputVolumeBalance);
        setAutoOutputVolumeBalanceStrength(config->AutoOutputVolumeBalanceStrength);
        config->setInitialTempValues();
        m_settingsOpen = false;
        flushConfigSave();
    }

    bool AudioForVatsim::fuzzyMatchCallsign(const QString &callsign, const QString &compareTo) const
    {
        if(callsign.isEmpty() || compareTo.isEmpty())
        {
            return false;
        }

        QString prefixA;
        QString suffixA;
        QString prefixB;
        QString suffixB;
        this->getPrefixSuffix(callsign, prefixA, suffixA);
        this->getPrefixSuffix(compareTo, prefixB, suffixB);
        return (prefixA == prefixB) && (suffixA == suffixB);
    }

    void AudioForVatsim::getPrefixSuffix(const QString &callsign, QString &prefix, QString &suffix) const
    {
        const QRegularExpression separator("[(\\-|_)]");
        const QStringList parts = callsign.split(separator);

        prefix = parts.size() > 0 ? parts.first() : QString();
        suffix = parts.size() > 1 ? parts.last() : QString();
    }

    quint32 AudioForVatsim::getAliasFrequency(quint32 frequency) const
    {
        auto it = std::find_if(m_controllers.constBegin(), m_controllers.constEnd(), [&](const Controller &c)
        {
            return c.FrequencyHz == frequency;
        });

        if(it != m_controllers.constEnd())
        {
            auto alias = std::find_if(m_aliasedStations.constBegin(), m_aliasedStations.constEnd(), [&](const afv_native::afv::dto::Station &station)
            {
                return it->FrequencyHz == station.FrequencyAlias && fuzzyMatchCallsign(station.Name.c_str(), it->Callsign);
            });

            if(alias != m_aliasedStations.constEnd())
            {
                return alias->Frequency;
            }
        }

        return 0;
    }
}
