#pragma once
#include <QObject>
#include "controllers/controller.h"
namespace xpilot {
class ControllerManager : public QObject {
    Q_OBJECT
public:
    inline static ControllerManager *instance = nullptr;
    ControllerManager() { instance = this; }
signals:
    void controllerAdded(Controller controller);
    void controllerUpdated(Controller controller);
    void controllerDeleted(Controller controller);
};
}
