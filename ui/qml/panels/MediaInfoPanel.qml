import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme
    property string selectedTitle: "No media selected"
    property string selectedSubtitle: "Open a local file to populate MediaInfoModel-backed details."
    property var infoModel: []
    property var flagModel: []

    color: theme ? theme.panelColor : "#1c1c1c"
    radius: theme ? theme.panelRadius : 14
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme ? theme.edgePadding : 20
        spacing: 16

        Text {
            color: theme ? theme.textPrimaryColor : "#f3f3f3"
            text: "Media Info"
            font.family: theme ? theme.fontFamily : "Segoe UI"
            font.pixelSize: theme ? theme.sectionTitleSize : 16
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            radius: theme ? theme.controlRadius : 10
            color: "#151515"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                Text {
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    text: "Selected item"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }

                Text {
                    color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                    text: root.selectedTitle
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    text: root.selectedSubtitle
                    wrapMode: Text.WordWrap
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.bodySize : 13
                }
            }
        }

        Text {
            color: theme ? theme.textSecondaryColor : "#bebebe"
            text: "Stream details"
            font.family: theme ? theme.fontFamily : "Segoe UI"
            font.pixelSize: theme ? theme.bodySize : 13
            font.bold: true
        }

        Repeater {
            model: root.infoModel

            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                radius: root.theme ? root.theme.controlRadius : 10
                color: "#151515"
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12

                    Text {
                        Layout.preferredWidth: 92
                        color: root.theme ? root.theme.textMutedColor : "#858585"
                        text: modelData.key
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: root.theme ? root.theme.captionSize : 11
                    }

                    Text {
                        Layout.fillWidth: true
                        color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                        text: modelData.value
                        elide: Text.ElideRight
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: root.theme ? root.theme.bodySize : 13
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme ? theme.controlRadius : 10
            color: "#151515"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Text {
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    text: "Runtime flags"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.bodySize : 13
                    font.bold: true
                }

                Repeater {
                    model: root.flagModel

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: modelData.tone === "success" ? "#132114"
                              : modelData.tone === "warning" ? "#241a0d"
                              : "#261312"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: modelData.tone === "success" ? (root.theme ? root.theme.successColor : "#6ec16e")
                                      : modelData.tone === "warning" ? (root.theme ? root.theme.warningColor : "#f0b34a")
                                      : (root.theme ? root.theme.dangerColor : "#e06c63")
                            }

                            Text {
                                Layout.fillWidth: true
                                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                                text: modelData.label
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: root.theme ? root.theme.captionSize : 11
                            }
                        }
                    }
                }
            }
        }
    }
}
