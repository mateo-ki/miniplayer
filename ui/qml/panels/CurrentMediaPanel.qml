import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme

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
            text: "Current Media"
            font.family: theme ? theme.fontFamily : "Segoe UI"
            font.pixelSize: theme ? theme.sectionTitleSize : 16
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 176
            radius: theme ? theme.controlRadius : 10
            color: theme ? theme.panelRaisedColor : "#242424"
            border.color: theme ? theme.borderColor : "#353535"
            border.width: 1

            Column {
                anchors.centerIn: parent
                spacing: 8

                Rectangle {
                    width: 68
                    height: 68
                    radius: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: theme ? theme.accentMutedColor : "#7a4a17"

                    Text {
                        anchors.centerIn: parent
                        color: theme ? theme.accentColor : "#f28c28"
                        text: "♪"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 28
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: theme ? theme.textPrimaryColor : "#f3f3f3"
                    text: "Night Drive Session"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.bodySize : 13
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: theme ? theme.textMutedColor : "#858585"
                    text: "Unbound visuals • placeholder"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.captionSize : 11
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            radius: theme ? theme.controlRadius : 10
            color: "#151515"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Repeater {
                    model: [
                        { value: "14", label: "items" },
                        { value: "02", label: "queued" },
                        { value: "HD", label: "target" }
                    ]

                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                            text: modelData.value
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 18
                            font.bold: true
                        }

                        Text {
                            color: root.theme ? root.theme.textMutedColor : "#858585"
                            text: modelData.label
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.captionSize : 11
                        }
                    }
                }
            }
        }

        Text {
            color: theme ? theme.textSecondaryColor : "#bebebe"
            text: "Up next"
            font.family: theme ? theme.fontFamily : "Segoe UI"
            font.pixelSize: theme ? theme.bodySize : 13
            font.bold: true
        }

        Repeater {
            model: [
                { title: "Ocean Avenue.mp4", meta: "00:03:42 • H.264 • pending" },
                { title: "Quiet Signals.flac", meta: "00:05:16 • stereo • pending" },
                { title: "Studio Reel.mov", meta: "00:01:58 • ProRes • pending" }
            ]

            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                radius: root.theme ? root.theme.controlRadius : 10
                color: index === 0 ? "#191919" : "#151515"
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        radius: 8
                        color: index === 0 ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                           : (root.theme ? root.theme.panelRaisedColor : "#242424")

                        Text {
                            anchors.centerIn: parent
                            color: index === 0 ? (root.theme ? root.theme.accentColor : "#f28c28")
                                               : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                            text: index + 1
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                            text: modelData.title
                            elide: Text.ElideRight
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.bodySize : 13
                        }

                        Text {
                            Layout.fillWidth: true
                            color: root.theme ? root.theme.textMutedColor : "#858585"
                            text: modelData.meta
                            elide: Text.ElideRight
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.captionSize : 11
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
