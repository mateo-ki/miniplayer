import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme

    color: theme ? theme.chromeColor : "#171717"
    radius: theme ? theme.panelRadius : 14
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme ? theme.edgePadding : 20
        spacing: 14

        RowLayout {
            spacing: 10

            Text {
                color: theme ? theme.textSecondaryColor : "#bebebe"
                text: "00:53"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.bodySize : 13
            }

            Slider {
                id: timeline
                Layout.fillWidth: true
                from: 0
                to: 100
                value: 32

                background: Rectangle {
                    x: timeline.leftPadding
                    y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                    width: timeline.availableWidth
                    height: 6
                    radius: 3
                    color: root.theme ? root.theme.panelRaisedColor : "#242424"

                    Rectangle {
                        width: timeline.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: root.theme ? root.theme.accentColor : "#f28c28"
                    }
                }

                handle: Rectangle {
                    x: timeline.leftPadding + timeline.visualPosition * (timeline.availableWidth - width)
                    y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                    border.color: root.theme ? root.theme.accentColor : "#f28c28"
                    border.width: 2
                }
            }

            Text {
                color: theme ? theme.textSecondaryColor : "#bebebe"
                text: "03:42"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.bodySize : 13
            }
        }

        RowLayout {
            spacing: 12

            Repeater {
                model: [
                    { symbol: "|<", emphasized: false },
                    { symbol: "||", emphasized: false },
                    { symbol: ">", emphasized: true },
                    { symbol: ">|", emphasized: false },
                    { symbol: "[]", emphasized: false }
                ]

                delegate: ToolButton {
                    id: transportButton

                    Layout.preferredWidth: modelData.emphasized ? 56 : 48
                    Layout.preferredHeight: modelData.emphasized ? 56 : 48

                    background: Rectangle {
                        radius: width / 2
                        color: modelData.emphasized
                            ? (root.theme ? root.theme.accentColor : "#f28c28")
                            : (transportButton.hovered
                                ? (root.theme ? root.theme.panelRaisedColor : "#242424")
                                : (root.theme ? root.theme.panelColor : "#1c1c1c"))
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: modelData.emphasized ? "#101010" : (root.theme ? root.theme.textPrimaryColor : "#f3f3f3")
                        text: modelData.symbol
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: modelData.emphasized ? 20 : 16
                        font.bold: modelData.emphasized
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 180
                Layout.preferredHeight: 44
                radius: theme ? theme.controlRadius : 10
                color: theme ? theme.panelColor : "#1c1c1c"
                border.color: theme ? theme.subtleBorderColor : "#282828"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    Text {
                        color: theme ? theme.textSecondaryColor : "#bebebe"
                        text: "Volume"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: theme ? theme.bodySize : 13
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: 74
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Repeater {
                model: [
                    { label: "Repeat off" },
                    { label: "Subtitles ready" },
                    { label: "Renderer: QML shell" }
                ]

                delegate: Rectangle {
                    Layout.preferredHeight: 38
                    Layout.preferredWidth: implicitWidth
                    implicitWidth: tagText.implicitWidth + 24
                    radius: root.theme ? root.theme.controlRadius : 10
                    color: root.theme ? root.theme.panelColor : "#1c1c1c"
                    border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                    border.width: 1

                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        color: root.theme ? root.theme.textMutedColor : "#858585"
                        text: modelData.label
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: root.theme ? root.theme.captionSize : 11
                    }
                }
            }
        }
    }
}
