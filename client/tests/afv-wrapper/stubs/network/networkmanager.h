#pragma once
#include <QObject>
#include <QString>
namespace xpilot {
class NetworkManager : public QObject {
    Q_OBJECT
public:
    inline static NetworkManager *instance = nullptr;
    NetworkManager() { instance = this; }
signals:
    void networkConnected(QString callsign, bool enableVoice);
    void networkDisconnected();
    void disableVoiceTransmit();
    void muteReceived(bool mute);
};
}
