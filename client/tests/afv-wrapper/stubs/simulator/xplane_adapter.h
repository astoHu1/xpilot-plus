#pragma once
#include <QObject>
#include <QString>
#include "aircrafts/radio_stack_state.h"
#include "aircrafts/user_aircraft_data.h"
class XplaneAdapter : public QObject {
    Q_OBJECT
public:
    inline static XplaneAdapter *instance = nullptr;
    bool rx[2]{};
    XplaneAdapter() { instance = this; }
    void setComRxDataref(int radio, bool active) { rx[radio] = active; }
    void setVuDataref(float) {}
    void setCom1OnHeadset(bool) {}
    void setCom2OnHeadset(bool) {}
    void setSplitAudioChannels(bool) {}
    void NotificationPosted(QString, qint64) {}
    void EnableVoiceTransmit() {}
    void DisableVoiceTransmit() {}
signals:
    void radioStackStateChanged(RadioStackState state);
    void userAircraftDataChanged(UserAircraftData data);
    void pttPressed();
    void pttReleased();
    void com1OnHeadsetChanged(bool value);
    void com2OnHeadsetChanged(bool value);
    void splitAudioChannelsChanged(bool value);
};
