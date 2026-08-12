import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    signal navigateRequested(int pageIndex)
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
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            Layout.fillWidth: true
                            color: root.theme.textPrimaryColor
                            text: "鎾斁鍘嗗彶璁板綍"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.sectionTitleSize
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            color: root.theme.textMutedColor
                            text: "点击记录可切回播放器并重新播放"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 32
                        radius: root.theme.controlRadius
                        color: clearHistoryHover.hovered ? root.theme.panelRaisedColor : "transparent"
                        border.color: root.theme.borderColor
                        border.width: 1
                        enabled: root.controller.historyModel.count > 0
                        opacity: enabled ? 1.0 : 0.45

                        HoverHandler { id: clearHistoryHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.controller.clearPlaybackHistory()
                        }

                        Text {
                            anchors.centerIn: parent
                            color: root.theme.textSecondaryColor
                            text: "娓呯┖"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                        }
                    }
                }

                ListView {
                    id: historyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    model: root.controller.historyModel

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 72
                        radius: root.theme.controlRadius
                        color: historyHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor
                        border.color: historyHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor
                        border.width: 1

                        HoverHandler { id: historyHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.navigateRequested(1)
                                root.controller.playFromHistory(index)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                radius: 10
                                color: root.theme.accentMutedColor

                                Text {
                                    anchors.centerIn: parent
                                    color: root.theme.accentColor
                                    text: index + 1
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    Layout.fillWidth: true
                                    color: root.theme.textPrimaryColor
                                    text: title
                                    elide: Text.ElideRight
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    color: root.theme.textMutedColor
                                    text: filePath
                                    elide: Text.ElideRight
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                }
                            }

                            Text {
                                Layout.preferredWidth: 76
                                color: root.theme.textMutedColor
                                text: lastPlayed
                                horizontalAlignment: Text.AlignRight
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.captionSize
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.controller.historyModel.count === 0
                        color: root.theme.textMutedColor
                        text: "鏆傛棤鎾斁鍘嗗彶"
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                    }
                }
            }
        }
    }
}
