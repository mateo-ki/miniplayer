import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"

    property var controller
    property var theme

    readonly property bool isChecking: controller ? controller.updateChecking : false
    readonly property bool isDownloading: controller ? controller.updateDownloading : false
    readonly property bool hasUpdate: controller ? controller.updateAvailable : false
    readonly property string versionText: controller ? controller.appVersion : "1.0"
    readonly property string newVersionText: controller ? controller.updateVersion : ""
    readonly property string messageText: controller ? controller.updateMessage : ""
    readonly property double progressVal: controller ? controller.updateDownloadProgress : 0.0
    readonly property string lastCheckText: controller ? controller.lastCheckTimeString : "尚未检查"

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentCol.implicitHeight + (root.theme ? root.theme.edgePadding * 2 : 40)
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentCol
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - (root.theme ? root.theme.edgePadding * 2 : 40), 760)
            anchors.top: parent.top
            anchors.topMargin: root.theme ? root.theme.edgePadding : 20
            spacing: 20

            // 顶部 Header 标题
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Rectangle {
                    width: 44
                    height: 44
                    radius: 12
                    color: root.theme ? root.theme.surfaceColor : "#18181c"
                    border.color: root.theme ? root.theme.subtleBorderColor : "#2a2a30"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "\uE777"
                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                        font.pixelSize: 22
                        color: root.theme ? root.theme.accentColor : "#38bdf8"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "软件更新"
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 22
                        font.bold: true
                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                    }

                    Text {
                        text: "保持 MeloBox 处于最新状态，获取最新特性与稳定性改进。"
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 13
                        color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                    }
                }
            }

            // 更新状态卡片
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: cardCol.implicitHeight + 48
                radius: root.theme ? root.theme.panelRadius : 12
                color: root.theme ? root.theme.panelColor : "#1c1c20"
                border.color: root.theme ? root.theme.borderColor : "#2a2a30"
                border.width: 1

                ColumnLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 20

                    // 版本与周期信息行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Rectangle {
                            width: 60
                            height: 60
                            radius: 14
                            color: root.theme ? root.theme.surfaceColor : "#18181c"
                            border.color: root.hasUpdate ? (root.theme ? root.theme.accentColor : "#38bdf8") : (root.theme ? root.theme.subtleBorderColor : "#2a2a30")
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: root.hasUpdate ? "\uE896" : "\uE777"
                                font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                font.pixelSize: 28
                                color: root.hasUpdate ? (root.theme ? root.theme.accentColor : "#38bdf8") : (root.theme ? root.theme.textSecondaryColor : "#a0a0ac")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            RowLayout {
                                spacing: 10
                                Text {
                                    text: "MeloBox 桌面客户端"
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                }

                                Rectangle {
                                    radius: 6
                                    color: root.theme ? root.theme.panelRaisedColor : "#26262c"
                                    implicitWidth: verLabel.implicitWidth + 12
                                    implicitHeight: 22

                                    Text {
                                        id: verLabel
                                        anchors.centerIn: parent
                                        text: "v" + root.versionText
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 11
                                        font.bold: true
                                        color: root.theme ? root.theme.accentColor : "#38bdf8"
                                    }
                                }
                            }

                            RowLayout {
                                spacing: 16
                                RowLayout {
                                    spacing: 4
                                    Text {
                                        text: "\uE823"
                                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                        font.pixelSize: 12
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                    }
                                    Text {
                                        text: "后台自动检查：每 6 小时"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 12
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                    }
                                }

                                RowLayout {
                                    spacing: 4
                                    Text {
                                        text: "\uE81C"
                                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                        font.pixelSize: 12
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                    }
                                    Text {
                                        text: "上次检查：" + root.lastCheckText
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 12
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.theme ? root.theme.subtleBorderColor : "#2a2a30"
                    }

                    // 检查中状态
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.isChecking
                        spacing: 12

                        RowLayout {
                            spacing: 10
                            BusyIndicator {
                                running: root.isChecking
                                implicitWidth: 24
                                implicitHeight: 24
                            }
                            Text {
                                text: "正在检查更新，请稍候..."
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 14
                                color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                            }
                        }
                    }

                    // 下载中状态
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.isDownloading
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "正在下载更新安装包..."
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 14
                                color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: Math.round(root.progressVal * 100) + "%"
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 13
                                font.bold: true
                                color: root.theme ? root.theme.accentColor : "#38bdf8"
                            }
                        }

                        // 进度条背景
                        Rectangle {
                            Layout.fillWidth: true
                            height: 8
                            radius: 4
                            color: root.theme ? root.theme.surfaceColor : "#18181c"

                            Rectangle {
                                width: Math.max(0, Math.min(parent.width, parent.width * root.progressVal))
                                height: parent.height
                                radius: 4
                                color: root.theme ? root.theme.accentColor : "#38bdf8"
                                Behavior on width { NumberAnimation { duration: 150 } }
                            }
                        }
                    }

                    // 有新版本状态
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !root.isChecking && !root.isDownloading && root.hasUpdate
                        spacing: 14

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: updateInfoCol.implicitHeight + 24
                            radius: 8
                            color: root.theme ? root.theme.surfaceColor : "#18181c"
                            border.color: root.theme ? root.theme.borderColor : "#2a2a30"
                            border.width: 1

                            ColumnLayout {
                                id: updateInfoCol
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                RowLayout {
                                    spacing: 8
                                    Text {
                                        text: "\uE73E"
                                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                        font.pixelSize: 16
                                        color: root.theme ? root.theme.successColor : "#22c55e"
                                    }
                                    Text {
                                        text: "发现新版本：" + (root.newVersionText.length > 0 ? ("v" + root.newVersionText) : "")
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 15
                                        font.bold: true
                                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: root.messageText
                                    wrapMode: Text.WrapAnywhere
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 13
                                    lineHeight: 1.4
                                    color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                                    visible: root.messageText.length > 0
                                }
                            }
                        }

                        RowLayout {
                            spacing: 12
                            Rectangle {
                                implicitWidth: 150
                                implicitHeight: 38
                                radius: root.theme ? root.theme.controlRadius : 8
                                color: downloadBtnMouse.containsMouse ? Qt.lighter(root.theme ? root.theme.accentColor : "#38bdf8", 1.1) : (root.theme ? root.theme.accentColor : "#38bdf8")

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Text {
                                        text: "\uE896"
                                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                        font.pixelSize: 15
                                        color: "#0e1015"
                                    }
                                    Text {
                                        text: "立即下载并安装"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: "#0e1015"
                                    }
                                }

                                MouseArea {
                                    id: downloadBtnMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.controller)
                                            root.controller.downloadAndInstallUpdate()
                                    }
                                }
                            }

                            Rectangle {
                                implicitWidth: 110
                                implicitHeight: 38
                                radius: root.theme ? root.theme.controlRadius : 8
                                color: checkAgainMouse.containsMouse ? (root.theme ? root.theme.panelRaisedColor : "#26262c") : "transparent"
                                border.color: root.theme ? root.theme.borderColor : "#2a2a30"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "重新检查"
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 13
                                    color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                }

                                MouseArea {
                                    id: checkAgainMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.controller)
                                            root.controller.checkForUpdates()
                                    }
                                }
                            }
                        }
                    }

                    // 最新版本 / 暂无可用更新状态
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !root.isChecking && !root.isDownloading && !root.hasUpdate
                        spacing: 16

                        RowLayout {
                            spacing: 12
                            Rectangle {
                                width: 36
                                height: 36
                                radius: 18
                                color: (root.theme ? root.theme.successColor : "#22c55e") + "20"

                                Text {
                                    anchors.centerIn: parent
                                    text: "\uE73E"
                                    font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                    font.pixelSize: 18
                                    color: root.theme ? root.theme.successColor : "#22c55e"
                                }
                            }

                            ColumnLayout {
                                spacing: 2
                                Text {
                                    text: root.messageText.length > 0 ? root.messageText : "当前已是最新版本"
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 15
                                    font.bold: true
                                    color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                }
                                Text {
                                    text: "无需更新，程序会自动在后台每 6 小时检查新版本。"
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 12
                                    color: root.theme ? root.theme.textMutedColor : "#737380"
                                }
                            }
                        }

                        Rectangle {
                            implicitWidth: 120
                            implicitHeight: 36
                            radius: root.theme ? root.theme.controlRadius : 8
                            color: checkManualMouse.containsMouse ? (root.theme ? root.theme.panelRaisedColor : "#26262c") : (root.theme ? root.theme.surfaceColor : "#18181c")
                            border.color: root.theme ? root.theme.borderColor : "#2a2a30"
                            border.width: 1

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6
                                Text {
                                    text: "\uE777"
                                    font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                    font.pixelSize: 13
                                    color: root.theme ? root.theme.accentColor : "#38bdf8"
                                }
                                Text {
                                    text: "检查更新"
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 13
                                    color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                }
                            }

                            MouseArea {
                                id: checkManualMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.controller)
                                        root.controller.checkForUpdates()
                                }
                            }
                        }
                    }
                }
            }

            // 底部自动更新机制卡片说明
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: tipCol.implicitHeight + 28
                radius: root.theme ? root.theme.panelRadius : 12
                color: root.theme ? root.theme.surfaceColor : "#18181c"
                border.color: root.theme ? root.theme.subtleBorderColor : "#2a2a30"
                border.width: 1

                ColumnLayout {
                    id: tipCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6

                    RowLayout {
                        spacing: 8
                        Text {
                            text: "\uE946"
                            font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                            font.pixelSize: 14
                            color: root.theme ? root.theme.textMutedColor : "#737380"
                        }
                        Text {
                            text: "关于自动更新机制"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 13
                            font.bold: true
                            color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "MeloBox 内置自动更新检测服务，应用启动后将自动检测，之后每隔 6 个小时在后台静默检查一次新版本。\n检测到新版本时，左侧侧边栏更新标签将显示红点提醒，随时可在此一键升级。"
                        wrapMode: Text.WrapAnywhere
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 12
                        lineHeight: 1.4
                        color: root.theme ? root.theme.textMutedColor : "#737380"
                    }
                }
            }
        }
    }
}
