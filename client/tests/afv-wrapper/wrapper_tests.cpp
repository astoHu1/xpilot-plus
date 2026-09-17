#include <QtTest>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QDir>
#include <cmath>
#include <limits>
#include <thread>
#include "audio/afv.h"
#include "config/appconfig.h"
#include "common/build_config.h"

using namespace xpilot;
const quint64 BuildConfig::ConfigEncryptionKey() { return 0; }
const QString BuildConfig::getVersionString() { return "offline-test"; }

class WrapperTests : public QObject {
    Q_OBJECT
    XplaneAdapter xplane;
    NetworkManager network;
    ControllerManager controllers;
    AppConfig *config = nullptr;

    static QObject *control(QObject *root, const char *property, const QString &value) {
        if(root->property(property).toString() == value) return root;
        for(auto *child : root->children()) {
            if(auto *found = control(child, property, value)) return found;
        }
        return nullptr;
    }
    void writeConfig(const QJsonObject &json) {
        QFile file(AppConfig::dataRoot() + "AppConfig.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(json).toJson()) > 0);
        file.close();
        config->loadConfig();
    }
    QJsonObject readConfig() {
        QFile file(AppConfig::dataRoot() + "AppConfig.json");
        if(!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
private slots:
    void initTestCase() {
        config = AppConfig::getInstance();
        qmlRegisterSingletonInstance("org.vatsim.xpilot", 1, 0, "AppConfig", config);

    }
    void init() {
        writeConfig({});
        xplane.rx[0] = xplane.rx[1] = false;
        afv_native::audio::AudioDevice::inputs = {{0, {"Mic A"}}, {1, {"Mic B"}}};
        afv_native::audio::AudioDevice::outputs = {{0, {"Output A"}}, {1, {"Output B"}}};
    }
    void configDefaultsAndValidation() {
        QVERIFY(!config->AutoOutputVolumeBalance);
        QCOMPARE(config->AutoOutputVolumeBalanceStrength, 60);
        writeConfig({{"Com1Volume", 59.9}, {"Com2Volume", 1e100},
                     {"MicrophoneVolume", -5.5}, {"AutoOutputVolumeBalanceStrength", 59.9},
                     {"AutoOutputVolumeBalance", true}});
        QCOMPARE(config->Com1Volume, 60);
        QCOMPARE(config->Com2Volume, 100);
        QCOMPARE(config->MicrophoneVolume, -5);
        QCOMPARE(config->AutoOutputVolumeBalanceStrength, 60);
        QVERIFY(config->AutoOutputVolumeBalance);
        writeConfig({{"Com1Volume", "NaN"}, {"Com2Volume", QJsonValue::Null},
                     {"AutoOutputVolumeBalanceStrength", "garbage"}, {"MicrophoneVolume", -1e100}});
        QCOMPARE(config->Com1Volume, 50);
        QCOMPARE(config->Com2Volume, 50);
        QCOMPARE(config->AutoOutputVolumeBalanceStrength, 60);
        QCOMPARE(config->MicrophoneVolume, -60);
    }
    void previewCancelAndApply() {
        AudioForVatsim wrapper;
        auto *native = afv_native::Client::instance;
        QSignalSpy writes(config, &AppConfig::settingsChanged);
        wrapper.settingsWindowOpened();
        config->setCom1Volume(71);
        wrapper.setCom1Volume(71);
        config->setAutoOutputVolumeBalance(true);
        wrapper.setAutoOutputVolumeBalance(true);
        config->setInputDevice("Mic B");
        wrapper.setInputDevice("Mic B");
        QCOMPARE(config->Com1Volume, 50);
        QVERIFY(native->balance);
        QTest::qWait(300);
        QCOMPARE(writes.count(), 0);
        wrapper.settingsWindowClosed(); // Cancel
        QCOMPARE(native->input, std::string());
        QVERIFY(!native->balance);
        QVERIFY(std::abs(native->gains[0] - (1.0f - std::sqrt(0.75f))) < 0.0001f);
        QCOMPARE(writes.count(), 0);

        wrapper.settingsWindowOpened();
        native->failMicrophone = true;
        config->setInputDevice("Mic B");
        wrapper.setInputDevice("Mic B");
        wrapper.settingsWindowClosed();
        QCOMPARE(native->input, std::string()); // cancel failed preview too
        native->failMicrophone = false;

        wrapper.settingsWindowOpened();
        config->setCom1Volume(71);
        wrapper.setCom1Volume(71);
        config->setAutoOutputVolumeBalance(true);
        wrapper.setAutoOutputVolumeBalance(true);
        config->applySettings();
        QCOMPARE(writes.count(), 1);
        QCOMPARE(readConfig()["Com1Volume"].toInt(), 71);
        config->setCom1Volume(10);
        wrapper.setCom1Volume(10);
        wrapper.settingsWindowClosed(); // Cancel only the preview after Apply
        QCOMPARE(config->Com1Volume, 71);
        QVERIFY(native->balance);
        QVERIFY(std::abs(native->gains[0] - (1.0f - std::sqrt(1.0f - 0.71f * 0.71f))) < 0.0001f);
    }
    void volumeDebounceAndBounds() {
        AudioForVatsim wrapper;
        auto *native = afv_native::Client::instance;
        QSignalSpy writes(config, &AppConfig::settingsChanged);
        for(int i = 0; i < 70; ++i) wrapper.setCom1Volume(i);
        wrapper.setCom1Volume(59.9);
        QCOMPARE(config->Com1Volume, 60);
        wrapper.setCom1Volume(std::numeric_limits<double>::quiet_NaN());
        wrapper.setCom1Volume(std::numeric_limits<double>::infinity());
        QCOMPARE(config->Com1Volume, 60);
        QVERIFY(std::isfinite(native->gains[0]));
        wrapper.setCom2Volume(-100);
        wrapper.setAutoOutputVolumeBalanceStrength(999);
        wrapper.setMicrophoneVolume(-999);
        QCOMPARE(native->gains[1], 0.0f);
        QCOMPARE(native->strength, 1.0f);
        QCOMPARE(native->microphone, -60);
        QCOMPARE(writes.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(writes.count(), 1, 1000);
        QCOMPARE(readConfig()["Com1Volume"].toInt(), 60);
        wrapper.setCom1Volume(99);
        wrapper.settingsWindowOpened(); // Flush pending changes before staging
        QCOMPARE(writes.count(), 2);
        wrapper.settingsWindowClosed();
    }
    void deviceAndRadioGuards() {
        AudioForVatsim wrapper;
        auto *native = afv_native::Client::instance;
        const int starts = native->starts;
        const int headsetCalls = native->headsetCalls;
        wrapper.setInputDevice("");
        wrapper.setHeadsetDevice("unplugged");
        wrapper.setOnHeadset(2, true);
        wrapper.setSplitAudioChannels(false);
        QCOMPARE(native->starts, starts);
        QCOMPARE(native->headsetCalls, headsetCalls);
        wrapper.setInputDevice("Mic A");
        wrapper.setInputDevice("Mic A");
        QCOMPARE(native->starts, starts + 1);
    }
    void missingDevicesRetryAndCalibration() {
        writeConfig({{"InputDevice", "Mic A"}, {"HeadsetDevice", "Output A"},
                     {"SpeakerDevice", "Output B"}});
        AudioForVatsim wrapper;
        auto *native = afv_native::Client::instance;
        native->stopAudio(); // disconnect releases devices but names remain selected
        const int beforeCalibration = native->starts;
        wrapper.settingsWindowOpened();
        QVERIFY(native->getMicrophoneDevice());
        QCOMPARE(native->starts, beforeCalibration + 1);
        wrapper.settingsWindowClosed();

        native->stopAudio();
        native->failMicrophone = native->failHeadset = native->failSpeaker = true;
        wrapper.setInputDevice("Mic A");
        wrapper.setHeadsetDevice("Output A");
        wrapper.setSpeakerDevice("Output B");
        QVERIFY(!native->getMicrophoneDevice());
        QVERIFY(!native->getHeadsetDevice());
        QVERIFY(!native->getSpeakerDevice());
        const int failedStarts = native->starts;
        native->failMicrophone = native->failHeadset = native->failSpeaker = false;
        wrapper.setInputDevice("Mic A");
        wrapper.setHeadsetDevice("Output A");
        wrapper.setSpeakerDevice("Output B");
        QVERIFY(native->getMicrophoneDevice());
        QVERIFY(native->getHeadsetDevice());
        QVERIFY(native->getSpeakerDevice());
        QCOMPARE(native->starts, failedStarts + 3);
        wrapper.setInputDevice("Mic A");
        wrapper.setHeadsetDevice("Output A");
        wrapper.setSpeakerDevice("Output B");
        QCOMPARE(native->starts, failedStarts + 3);
    }
    void copiedErrorAndQueuedAlias() {
        AudioForVatsim wrapper;
        auto *native = afv_native::Client::instance;
        QSignalSpy messages(&wrapper, &AudioForVatsim::notificationPosted);
        QSignalSpy aliases(&wrapper, &AudioForVatsim::radioAliasChanged);
        std::atomic<bool> finished{false};
        native->base->enqueue([&] {
            char error[] = "temporary message";
            native->ClientEventCallback.invokeAll(afv_native::ClientEventType::AudioError, error, nullptr);
            std::fill(std::begin(error), std::end(error) - 1, 'x');
            native->aliases = {{"TEST_TWR", 118000000, 122000000}};
            native->ClientEventCallback.invokeAll(afv_native::ClientEventType::StationAliasesUpdated, nullptr, nullptr);
            finished = true;
        });
        QTRY_VERIFY(finished.load());
        // Both callbacks have queued their payloads; drain delivery before querying
        // aliases instead of depending on the polling macro to pump one more turn.
        QCoreApplication::sendPostedEvents(&wrapper, QEvent::MetaCall);
        QTRY_COMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(0).toString(), QString("temporary message"));
        Controller controller{};
        controller.Callsign = "TEST_TWR";
        controller.FrequencyHz = 118000000;
        emit controllers.controllerAdded(controller);
        RadioStackState state{};
        state.Com1Frequency = 118000;
        emit xplane.radioStackStateChanged(state);
        QCOMPARE(aliases.at(0).at(1).toUInt(), 122000000u);
    }
    void rxDatarefMatchesUi() {
        AudioForVatsim wrapper;
        QSignalSpy rx(&wrapper, &AudioForVatsim::radioRxChanged);
        RadioStackState state{};
        state.Com1ReceiveEnabled = true;
        state.Com2ReceiveEnabled = false;
        emit xplane.radioStackStateChanged(state);
        emit network.networkConnected("OFFLINE", true); // double: opens no socket
        QTRY_VERIFY(rx.count() >= 2);
        QVERIFY(!xplane.rx[0] && !xplane.rx[1]); // avionics off
        state.OverrideRadioPower = true;
        emit xplane.radioStackStateChanged(state);
        QTRY_VERIFY(xplane.rx[0]);
        QVERIFY(!xplane.rx[1]);
        QCOMPARE(rx.at(rx.count() - 2).at(1).toBool(), xplane.rx[0]);
        QCOMPARE(rx.last().at(1).toBool(), xplane.rx[1]);
        emit network.networkDisconnected();
        QVERIFY(!xplane.rx[0] && !xplane.rx[1]);
    }
    void shutdownWaitsAndReleases() {
        const int freed = event_base::released;
        const int destroyed = afv_native::Client::destroyed;
        std::atomic<bool> started{false}, finished{false};
        auto wrapper = std::make_unique<AudioForVatsim>();
        auto *native = afv_native::Client::instance;
        native->base->enqueue([&] {
            started = true;
            QThread::msleep(40);
            char message[] = "shutdown message";
            native->ClientEventCallback.invokeAll(afv_native::ClientEventType::AudioError, message, nullptr);
            finished = true;
        });
        while(!started.load()) QThread::msleep(1);
        wrapper.reset();
        QVERIFY(finished.load());
        QCOMPARE(event_base::released.load(), freed + 1);
        QCOMPARE(afv_native::Client::destroyed, destroyed + 1);
        QVERIFY(afv_native::logger == nullptr);
        QCoreApplication::processEvents(); // queued callbacks must be discarded
    }
    void qmlInitializationRefreshAndRounding() {
        writeConfig({{"InputDevice", "Mic A"}, {"HeadsetDevice", "Output A"},
                     {"SpeakerDevice", "Output B"}, {"AutoOutputVolumeBalance", true}});
        AudioForVatsim wrapper;
        auto *native = afv_native::Client::instance;
        const int starts = native->starts;
        QSignalSpy writes(config, &AppConfig::settingsChanged);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("audio", &wrapper);
        engine.rootContext()->setContextProperty("xplaneAdapter", &xplane);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(CLIENT_SOURCE_DIR) + "/Resources/Views/Settings/SettingsAudio.qml"));
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        QSignalSpy changes(page.get(), SIGNAL(applyChanges()));
        QCOMPARE(native->starts, starts);
        QCOMPARE(writes.count(), 0);
        emit xplane.splitAudioChannelsChanged(true);
        QVERIFY(native->split);
        QVERIFY(!config->SplitAudioChannels); // preview remains staged
        QCOMPARE(changes.count(), 1);
        config->applySettings();
        QVERIFY(config->SplitAudioChannels);
        emit xplane.splitAudioChannelsChanged(false);
        QVERIFY(!native->split);
        wrapper.settingsWindowClosed(); // Cancel restores the applied simulator value
        QVERIFY(native->split);
        wrapper.settingsWindowOpened();
        auto *input = control(page.get(), "fieldLabel", "Microphone Device:");
        auto *balance = control(page.get(), "comLabel", "Balance");
        auto *mic = control(page.get(), "comLabel", "Mic Volume");
        QVERIFY(input && balance && mic);
        input->setProperty("currentIndex", 1); // select Mic B
        QCOMPARE(native->input, std::string("Mic B"));
        const int userStarts = native->starts;
        changes.clear();
        afv_native::audio::AudioDevice::inputs.clear();
        QVERIFY(QMetaObject::invokeMethod(&wrapper, "OnAudioDevicesTimer"));
        QCOMPARE(input->property("currentIndex").toInt(), -1);
        afv_native::audio::AudioDevice::inputs = {{0, {"Mic B"}}, {1, {"Mic A"}}};
        QVERIFY(QMetaObject::invokeMethod(&wrapper, "OnAudioDevicesTimer"));
        QCOMPARE(input->property("currentIndex").toInt(), 0);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(native->starts, userStarts);
        QVERIFY(QMetaObject::invokeMethod(balance, "volumeValueChanged", Q_ARG(double, 59.9)));
        QCOMPARE(native->strength, 0.6f);
        QVERIFY(QMetaObject::invokeMethod(mic, "volumeValueChanged", Q_ARG(double, -5.5)));
        QCOMPARE(native->microphone, -5);
        QVERIFY(QMetaObject::invokeMethod(balance, "volumeValueChanged", Q_ARG(double, std::numeric_limits<double>::infinity())));
        QCOMPARE(native->strength, 0.6f);
        QVERIFY(QMetaObject::invokeMethod(mic, "volumeValueChanged", Q_ARG(double, 1000)));
        QCOMPARE(native->microphone, 18);
        config->applySettings();
        QCOMPARE(config->InputDevice, QString("Mic B"));
        QCOMPARE(config->AutoOutputVolumeBalanceStrength, 60);
        QCOMPARE(config->MicrophoneVolume, 18);
        wrapper.settingsWindowClosed();
    }
};

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    const auto root = qEnvironmentVariable("AFV_TEST_HOME");
    if(root.isEmpty() || !QDir::cleanPath(AppConfig::dataRoot()).startsWith(QDir::cleanPath(root) + "/")) {
        qCritical("Refusing to use non-isolated config; run via CTest/run_isolated.py.");
        return 2;
    }
    WrapperTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "wrapper_tests.moc"
