import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    property var strings
    signal saveUiSettingRequested(string key, var value)
    property int selectedVoiceIndex: 0
    Component.onCompleted: selectedVoiceIndex = root.controller.uiSetting("selectedVoiceIndex", selectedVoiceIndex)
    onSelectedVoiceIndexChanged: root.saveUiSettingRequested("selectedVoiceIndex", selectedVoiceIndex)
    readonly property var sources: [
        { title: "可爱配音", subtitle: "and.php", sourceIndex: 0 },
        { title: "御姐撒娇语音", subtitle: "yujie.php", sourceIndex: 1 },
        { title: "坤坤语音", subtitle: "sjkunkun.php", sourceIndex: 2 },
        { title: "绿茶", subtitle: "lvcha.php", sourceIndex: 3 },
        { title: "语音整点报时", subtitle: "baoshi.php?msg=本机时间", sourceIndex: 4 }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.theme.edgePadding
        spacing: root.theme.gap

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.theme.panelRadius
            color: root.theme.panelColor
            border.color: root.theme.subtleBorderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.theme.edgePadding
                spacing: 18

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: root.strings.voiceTitle
                        color: root.theme.textPrimaryColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.sectionTitleSize
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.strings.voiceDescription
                        color: root.theme.textMutedColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.captionSize
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignRight
                    spacing: 10

                    ComboBox {
                        id: voiceSourceMenu
                        Layout.preferredWidth: 220
                        Layout.preferredHeight: 34
                        model: root.sources
                        textRole: "title"
                        valueRole: "sourceIndex"
                        currentIndex: root.selectedVoiceIndex
                        onActivated: function(index) {
                            root.selectedVoiceIndex = model[index].sourceIndex
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 34
                        radius: root.theme.controlRadius
                        color: playVoiceHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor

                        HoverHandler { id: playVoiceHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.controller.playVoice(root.selectedVoiceIndex)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: root.strings.playLabel
                            color: "#ffffff"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            font.bold: true
                        }
                    }

                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Repeater {
                        model: root.sources

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 190
                            radius: root.theme.panelRadius
                            color: voiceHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor
                            border.color: voiceHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor
                            border.width: 1

                            HoverHandler { id: voiceHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.selectedVoiceIndex = modelData.sourceIndex
                                    root.controller.playVoice(modelData.sourceIndex)
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 48
                                    Layout.preferredHeight: 48
                                    radius: 16
                                    color: root.theme.accentMutedColor

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.strings.musicIcon
                                        color: root.theme.accentColor
                                        font.pixelSize: 24
                                        font.bold: true
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: root.theme.textPrimaryColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 17
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.subtitle
                                    color: root.theme.textMutedColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                    elide: Text.ElideRight
                                }

                                Item { Layout.fillHeight: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 30
                                        radius: root.theme.controlRadius
                                        color: voicePlayHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor

                                        HoverHandler { id: voicePlayHover }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                root.selectedVoiceIndex = modelData.sourceIndex
                                                root.controller.playVoice(modelData.sourceIndex)
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: root.strings.playLabel
                                            color: "#ffffff"
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: root.theme.captionSize
                                            font.bold: true
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 30
                                        radius: root.theme.controlRadius
                                        color: voiceCardDownloadHover.hovered ? root.theme.panelRaisedColor : root.theme.panelColor
                                        border.color: root.theme.subtleBorderColor
                                        border.width: 1

                                        HoverHandler { id: voiceCardDownloadHover }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                root.selectedVoiceIndex = modelData.sourceIndex
                                                root.controller.saveVoice(modelData.sourceIndex)
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: root.strings.downloadLabel
                                            color: root.theme.textSecondaryColor
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: root.theme.captionSize
                                            font.bold: true
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 30
                                        radius: root.theme.controlRadius
                                        color: voiceCardCopyHover.hovered ? root.theme.panelRaisedColor : root.theme.panelColor
                                        border.color: root.theme.subtleBorderColor
                                        border.width: 1

                                        HoverHandler { id: voiceCardCopyHover }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                root.selectedVoiceIndex = modelData.sourceIndex
                                                root.controller.copyVoice(modelData.sourceIndex)
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: root.strings.copyLabel
                                            color: root.theme.textSecondaryColor
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: root.theme.captionSize
                                            font.bold: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.controller.voiceMessage.length > 0
                    text: root.controller.voiceMessage + (root.controller.currentVoiceUrl.length > 0 ? " · " + root.controller.currentVoiceUrl : "")
                    color: root.theme.accentColor
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.captionSize
                    elide: Text.ElideRight
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
