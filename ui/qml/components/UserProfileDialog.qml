import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property var theme
    property var authController

    title: ""
    anchors.centerIn: parent
    modal: true
    width: 500
    implicitHeight: 560
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: root.theme ? root.theme.windowRadius : 14
        color: root.theme ? root.theme.panelColor : "#1f2023"
        border.color: root.theme ? root.theme.subtleBorderColor : "#26272b"
        border.width: 1
    }

    header: Rectangle {
        implicitHeight: 54
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 18

            RowLayout {
                spacing: 8

                Text {
                    text: "\uE77B"
                    font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                    color: root.theme ? root.theme.accentColor : "#10b981"
                    font.pixelSize: 18
                }

                Text {
                    text: "个人信息中心"
                    color: root.theme ? root.theme.textPrimaryColor : "#f4f7fb"
                    font.pixelSize: 16
                    font.bold: true
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 6
                color: closeMouse.containsMouse ? (root.theme ? root.theme.panelRaisedColor : "#2b2c30") : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "\uE711"
                    font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                    color: root.theme ? root.theme.textMutedColor : "#7f8a9c"
                    font.pixelSize: 12
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: root.theme ? root.theme.subtleBorderColor : "#26272b"
        }
    }

    contentItem: ColumnLayout {
        spacing: 16

        // 用户头部卡片
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 90
            radius: root.theme ? root.theme.panelRadius : 10
            color: root.theme ? root.theme.surfaceColor : "#161719"
            border.color: root.theme ? root.theme.subtleBorderColor : "#26272b"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16

                // 头像徽章
                Rectangle {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    radius: 28
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.theme ? root.theme.accentColor : "#10b981" }
                        GradientStop { position: 1.0; color: "#047857" }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: (root.authController && root.authController.username.length > 0)
                            ? root.authController.username.charAt(0).toUpperCase()
                            : "U"
                        color: "#141517"
                        font.pixelSize: 24
                        font.bold: true
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        spacing: 8

                        Text {
                            text: (root.authController && root.authController.username.length > 0)
                                ? root.authController.username
                                : "未登录"
                            color: root.theme ? root.theme.textPrimaryColor : "#f4f7fb"
                            font.pixelSize: 18
                            font.bold: true
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        }

                        // 管理员徽章
                        Rectangle {
                            visible: root.authController && root.authController.role === "admin"
                            implicitWidth: adminText.implicitWidth + 12
                            implicitHeight: 20
                            radius: 4
                            color: root.theme ? root.theme.accentMutedColor : "#14382c"
                            border.color: root.theme ? root.theme.accentColor : "#10b981"
                            border.width: 1

                            Text {
                                id: adminText
                                anchors.centerIn: parent
                                text: "管理员"
                                color: root.theme ? root.theme.accentColor : "#10b981"
                                font.pixelSize: 10
                                font.bold: true
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            }
                        }

                        // 等级徽章
                        Rectangle {
                            visible: root.authController && root.authController.tier.length > 0
                            implicitWidth: tierText.implicitWidth + 12
                            implicitHeight: 20
                            radius: 4
                            color: "#251838"
                            border.color: "#9c64e8"
                            border.width: 1

                            Text {
                                id: tierText
                                anchors.centerIn: parent
                                text: root.authController ? root.authController.tier.toUpperCase() : ""
                                color: "#bb86fc"
                                font.pixelSize: 10
                                font.bold: true
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            }
                        }
                    }

                    Text {
                        text: (root.authController && root.authController.email.length > 0)
                            ? root.authController.email
                            : "暂未获取邮箱"
                        color: root.theme ? root.theme.textMutedColor : "#7f8a9c"
                        font.pixelSize: 12
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    }
                }
            }
        }

        // 详细信息面板
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.theme ? root.theme.panelRadius : 10
            color: root.theme ? root.theme.panelRaisedColor : "#2b2c30"
            border.color: root.theme ? root.theme.borderColor : "#32343a"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                // 单条数据项组件
                component InfoRow: RowLayout {
                    property string label: ""
                    property string value: ""
                    property bool monospace: false
                    property color valueColor: root.theme ? root.theme.textPrimaryColor : "#f4f7fb"

                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: label
                        color: root.theme ? root.theme.textMutedColor : "#7f8a9c"
                        font.pixelSize: 12
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        Layout.preferredWidth: 90
                    }

                    Text {
                        text: value
                        color: valueColor
                        font.pixelSize: 12
                        font.family: monospace ? "Consolas" : (root.theme ? root.theme.fontFamily : "Segoe UI")
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                InfoRow {
                    label: "用户 ID"
                    value: root.authController ? root.authController.userId : "-"
                    monospace: true
                    valueColor: root.theme ? root.theme.textSecondaryColor : "#b9c2d0"
                }

                InfoRow {
                    label: "账户身份"
                    value: (root.authController && root.authController.role === "admin") ? "系统管理员 (admin)" : "普通用户"
                }

                InfoRow {
                    label: "等级权益"
                    value: root.authController ? (root.authController.tier + " 等级") : "-"
                }

                InfoRow {
                    label: "账户积分"
                    value: root.authController ? (root.authController.points + " 积分") : "0"
                    valueColor: root.theme ? root.theme.accentColor : "#10b981"
                }

                InfoRow {
                    label: "今日配额"
                    value: root.authController ? (root.authController.usedToday + " / " + root.authController.dailyLimit) : "0 / 0"
                    valueColor: root.theme ? root.theme.successColor : "#10b981"
                }

                InfoRow {
                    label: "订阅状态"
                    value: (root.authController && root.authController.subscriptionEnabled) ? "已生效 (有效订阅)" : "未订阅"
                    valueColor: (root.authController && root.authController.subscriptionEnabled)
                        ? (root.theme ? root.theme.successColor : "#54c48a")
                        : (root.theme ? root.theme.textMutedColor : "#7f8a9c")
                }

                InfoRow {
                    label: "LinuxDo 绑定"
                    value: (root.authController && root.authController.linuxdoBound) ? "已关联账号" : "未关联"
                }

                InfoRow {
                    label: "注册时间"
                    value: root.authController ? root.authController.createdAt : "-"
                    valueColor: root.theme ? root.theme.textSecondaryColor : "#b9c2d0"
                }

                InfoRow {
                    label: "上次登录"
                    value: root.authController ? root.authController.lastLoginAt : "-"
                    valueColor: root.theme ? root.theme.textSecondaryColor : "#b9c2d0"
                }
            }
        }

        // 底部操作按钮
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            // 退出登录按钮
            Rectangle {
                Layout.preferredWidth: 100
                implicitHeight: 36
                radius: root.theme ? root.theme.controlRadius : 8
                color: logoutMouse.containsMouse ? "#3b1e1e" : "#2a1515"
                border.color: root.theme ? root.theme.dangerColor : "#e06c63"
                border.width: 1

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6

                    Text {
                        text: "\uF3B1"
                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                        color: root.theme ? root.theme.dangerColor : "#ef4444"
                        font.pixelSize: 12
                    }

                    Text {
                        text: "退出登录"
                        color: root.theme ? root.theme.dangerColor : "#e06c63"
                        font.pixelSize: 12
                        font.bold: true
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    }
                }

                MouseArea {
                    id: logoutMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.authController) {
                            root.authController.logout()
                        }
                        root.close()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // 刷新信息按钮
            Rectangle {
                Layout.preferredWidth: 100
                implicitHeight: 36
                radius: root.theme ? root.theme.controlRadius : 8
                color: refreshMouse.containsMouse ? (root.theme ? root.theme.panelRaisedColor : "#2b2c30") : (root.theme ? root.theme.surfaceColor : "#161719")
                border.color: root.theme ? root.theme.borderColor : "#32343a"
                border.width: 1

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6

                    BusyIndicator {
                        running: root.authController && root.authController.isRefreshingUserInfo
                        visible: running
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                    }

                    Text {
                        visible: !root.authController || !root.authController.isRefreshingUserInfo
                        text: "\uE72C"
                        font.family: root.theme ? root.theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                        color: root.theme ? root.theme.textSecondaryColor : "#b9c2d0"
                        font.pixelSize: 12
                    }

                    Text {
                        text: "刷新数据"
                        color: root.theme ? root.theme.textSecondaryColor : "#b9c2d0"
                        font.pixelSize: 12
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    }
                }

                MouseArea {
                    id: refreshMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.authController) {
                            root.authController.refreshUserInfo()
                        }
                    }
                }
            }

            // 关闭按钮
            Rectangle {
                Layout.preferredWidth: 80
                implicitHeight: 36
                radius: root.theme ? root.theme.controlRadius : 8
                color: closeBtnMouse.containsMouse ? Qt.lighter(root.theme ? root.theme.accentColor : "#10b981", 1.1) : (root.theme ? root.theme.accentColor : "#10b981")

                Text {
                    anchors.centerIn: parent
                    text: "关 闭"
                    color: "#141517"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                }

                MouseArea {
                    id: closeBtnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }
    }
}
