import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.vatsim.xpilot
import "../../Controls"

Item {
    id: root

    signal applyChanges()

    property bool inputDeviceListLoaded: false
    property bool outputDeviceListLoaded: false
    property bool inputDeviceChanged: false
    property bool refreshingDevices: false
    property bool initializing: true

    function refreshInputDevices() {
        refreshingDevices = true
        inputDeviceList.model = audio.InputDevices
        inputDeviceList.currentIndex = inputDeviceList.indexOfValue(inputDeviceList.selectedDevice)
        inputDeviceListLoaded = true
        refreshingDevices = false
    }

    function refreshOutputDevices() {
        refreshingDevices = true
        headsetDeviceList.model = audio.OutputDevices
        speakerDeviceList.model = audio.OutputDevices
        headsetDeviceList.currentIndex = headsetDeviceList.indexOfValue(headsetDeviceList.selectedDevice)
        speakerDeviceList.currentIndex = speakerDeviceList.indexOfValue(speakerDeviceList.selectedDevice)
        outputDeviceListLoaded = true
        refreshingDevices = false
    }

    function roundedVolume(value, minimum, maximum) {
        return Math.round(Math.max(minimum, Math.min(maximum, value)))
    }

    Connections {
        target: audio

        function onInputDevicesChanged() {
            if(inputDeviceListLoaded) {
                refreshInputDevices()
            }
        }

        function onOutputDevicesChanged() {
            if(outputDeviceListLoaded) {
                refreshOutputDevices()
            }
        }

        function onInputVuChanged(vu) {
            peakLevel.value = vu
        }
    }

    Connections {
        target: xplaneAdapter

        function onSplitAudioChannelsChanged(split) {
            switchSplitComChannels.checked = split
            if(!root.initializing) {
                AppConfig.SplitAudioChannels = split
                applyChanges()
            }
        }
    }

    Component.onCompleted: {
        // Start staging before any controls can emit change signals.
        audio.settingsWindowOpened()
        inputDeviceList.selectedDevice = AppConfig.InputDevice
        headsetDeviceList.selectedDevice = AppConfig.HeadsetDevice
        speakerDeviceList.selectedDevice = AppConfig.SpeakerDevice
        refreshInputDevices()
        refreshOutputDevices()
        switchSplitComChannels.checked = AppConfig.SplitAudioChannels
        switchEnableHfSquelch.checked = AppConfig.HFSquelchEnabled
        switchDisableRadioEffects.checked = AppConfig.AudioEffectsDisabled
        switchAircraftVolumeKnobs.checked = AppConfig.AircraftRadioStackControlsVolume
        switchAutoOutputBalance.checked = AppConfig.AutoOutputVolumeBalance
        com1Slider.volume = AppConfig.Com1Volume
        com2Slider.volume = AppConfig.Com2Volume
        autoOutputBalanceStrength.volume = AppConfig.AutoOutputVolumeBalanceStrength
        microphoneVolume.volume = AppConfig.MicrophoneVolume
        initializing = false
    }

    ColumnLayout {
        spacing: 10
        width: 500

        CustomComboBox {
            id: inputDeviceList
            property string selectedDevice: ""
            fieldLabel: "Microphone Device:"
            valueRole: "name"
            textRole: "name"
            onSelectedValueChanged: function(value) {
                if(!root.initializing && !refreshingDevices && inputDeviceListLoaded &&
                   inputDeviceList.currentIndex >= 0 && value.length > 0 &&
                   value !== inputDeviceList.selectedDevice) {
                    inputDeviceList.selectedDevice = value
                    AppConfig.InputDevice = value
                    audio.setInputDevice(value)
                    inputDeviceChanged = true
                    applyChanges()
                }
            }
        }

        CustomComboBox {
            id: headsetDeviceList
            property string selectedDevice: ""
            fieldLabel: "Headset Device:"
            valueRole: "name"
            textRole: "name"
            onSelectedValueChanged: function(value) {
                if(!root.initializing && !refreshingDevices && outputDeviceListLoaded &&
                   headsetDeviceList.currentIndex >= 0 && value.length > 0 &&
                   value !== headsetDeviceList.selectedDevice) {
                    headsetDeviceList.selectedDevice = value
                    AppConfig.HeadsetDevice = value
                    audio.setHeadsetDevice(value)
                    applyChanges()
                }
            }
        }

        CustomComboBox {
            id: speakerDeviceList
            property string selectedDevice: ""
            fieldLabel: "Speaker Device:"
            valueRole: "name"
            textRole: "name"
            onSelectedValueChanged: function(value) {
                if(!root.initializing && !refreshingDevices && outputDeviceListLoaded &&
                   speakerDeviceList.currentIndex >= 0 && value.length > 0 &&
                   value !== speakerDeviceList.selectedDevice) {
                    speakerDeviceList.selectedDevice = value
                    AppConfig.SpeakerDevice = value
                    audio.setSpeakerDevice(value)
                    applyChanges()
                }
            }
        }

        RowLayout {
            spacing: 40

            ColumnLayout {
                width: 300
                spacing: 0
                Layout.alignment: Qt.AlignTop

                CustomSwitch {
                    id: switchSplitComChannels
                    text: "Split Output Audio Channels"
                    font.pixelSize: 13
                    leftPadding: 0
                    tooltipText: "Split output audio to separate channels (COM1 = Left, COM2 = Right)"
                    onToggled: {
                        if(root.initializing) return
                        AppConfig.SplitAudioChannels = switchSplitComChannels.checked
                        audio.setSplitAudioChannels(switchSplitComChannels.checked)
                        applyChanges()
                    }
                }

                CustomSwitch {
                    id: switchEnableHfSquelch
                    text: "Enable HF Squelch"
                    font.pixelSize: 13
                    leftPadding: 0
                    onToggled: {
                        if(root.initializing) return
                        AppConfig.HFSquelchEnabled = switchEnableHfSquelch.checked
                        audio.enableHfSquelch(switchEnableHfSquelch.checked)
                        applyChanges()
                    }
                }

                CustomSwitch {
                    id: switchDisableRadioEffects
                    text: "Disable Realistic Radio Effects"
                    leftPadding: 0
                    font.pixelSize: 13
                    onToggled: {
                        if(root.initializing) return
                        AppConfig.AudioEffectsDisabled = switchDisableRadioEffects.checked
                        audio.disableAudioEffects(switchDisableRadioEffects.checked)
                        applyChanges()
                    }
                }

                CustomSwitch {
                    id: switchAircraftVolumeKnobs
                    text: "Allow aircraft radio stack volume knobs to control (and override) radio volume"
                    font.pixelSize: 13
                    Layout.maximumWidth: 300
                    leftPadding: 0
                    onToggled: {
                        if(root.initializing) return
                        AppConfig.AircraftRadioStackControlsVolume = switchAircraftVolumeKnobs.checked
                        applyChanges()
                    }
                }

                CustomSwitch {
                    id: switchAutoOutputBalance
                    text: "Auto Balance Incoming Voice Volume"
                    font.pixelSize: 13
                    Layout.maximumWidth: 300
                    leftPadding: 0
                    tooltipText: "Balance loudness between incoming voices and control combined radio output"
                    onToggled: {
                        if(root.initializing) return
                        AppConfig.AutoOutputVolumeBalance = switchAutoOutputBalance.checked
                        audio.setAutoOutputVolumeBalance(switchAutoOutputBalance.checked)
                        applyChanges()
                    }
                }

                VolumeSlider {
                    id: com1Slider
                    comLabel: "COM1"
                    onVolumeValueChanged: function(volume) {
                        if(root.initializing || !isFinite(volume)) return
                        var value = roundedVolume(volume, 0, 100)
                        AppConfig.Com1Volume = value
                        audio.setCom1Volume(value)
                        applyChanges()
                    }
                }

                VolumeSlider {
                    id: com2Slider
                    comLabel: "COM2"
                    onVolumeValueChanged: function(volume) {
                        if(root.initializing || !isFinite(volume)) return
                        var value = roundedVolume(volume, 0, 100)
                        AppConfig.Com2Volume = value
                        audio.setCom2Volume(value)
                        applyChanges()
                    }
                }

                VolumeSlider {
                    id: autoOutputBalanceStrength
                    comLabel: "Balance"
                    Layout.preferredWidth: 300
                    Layout.maximumWidth: 300
                    enabled: switchAutoOutputBalance.checked
                    ToolTip.visible: balanceHover.hovered
                    ToolTip.text: "Adjust how strongly incoming voice loudness is balanced (0–100%)."
                    HoverHandler { id: balanceHover }
                    onVolumeValueChanged: function(volume) {
                        if(root.initializing || !isFinite(volume)) return
                        var value = roundedVolume(volume, 0, 100)
                        AppConfig.AutoOutputVolumeBalanceStrength = value
                        audio.setAutoOutputVolumeBalanceStrength(value)
                        applyChanges()
                    }
                }
            }

            ColumnLayout {
                width: 300
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 10
                Layout.rightMargin: 10

                PeakLevelControl {
                    id: peakLevel
                    height: 13
                }

                Text {
                    text: "Adjust the mic volume slider so that the peak level indicator remains green while speaking normally."
                    renderType: Text.NativeRendering
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: 300
                    topPadding: 5
                    font.pixelSize: 13
                    color: "#000000"
                }

                VolumeSlider {
                    id: microphoneVolume
                    comLabel: "Mic Volume"
                    minValue: -60
                    maxValue: 18
                    showPercent: false
                    onVolumeValueChanged: function(volume) {
                        if(root.initializing || !isFinite(volume)) return
                        var value = roundedVolume(volume, -60, 18)
                        AppConfig.MicrophoneVolume = value
                        audio.setMicrophoneVolume(value)
                        applyChanges()
                    }
                }
            }
        }

        Text {
            id: name
            text: "Your Push to Talk (PTT) must be assigned in X-Plane using the joystick or keyboard command bindings. <a href='http://xpilot-project.org/ptt-setup'>Learn more about how to set your PTT</a>"
            onLinkActivated: (link) => Qt.openUrlExternally(link)
            renderType: Text.NativeRendering
            wrapMode: Text.WordWrap
            Layout.maximumWidth: parent.width
            linkColor: "#0164AD"
            font.pixelSize: 13
            color: "#000000"

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }
    }
}
