import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var theme
    property var authController

    color: theme ? theme.windowColor : "#18191b"

    // 拦截下方所有鼠标点击
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
    }

    // 居中装饰背景光晕
    Rectangle {
        anchors.centerIn: card
        width: card.width + 60
        height: card.height + 60
        radius: 28
        color: theme ? theme.accentColor : "#10b981"
        opacity: 0.06
    }

    // 登录卡片
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 420
        implicitHeight: contentCol.implicitHeight + 56
        radius: theme ? theme.windowRadius : 14
        color: theme ? theme.panelColor : "#1f2023"
        border.color: theme ? theme.subtleBorderColor : "#26272b"
        border.width: 1

        property bool showPassword: false

        ColumnLayout {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 28
            spacing: 18

            // Logo 与标题
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 52
                    height: 52
                    radius: 14
                    color: theme ? theme.accentColor : "#10b981"

                    Text {
                        anchors.centerIn: parent
                        text: "M"
                        color: "#141517"
                        font.pixelSize: 26
                        font.bold: true
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "MeloBox"
                    color: theme ? theme.textPrimaryColor : "#f4f7fb"
                    font.pixelSize: 24
                    font.bold: true
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "聚合多媒体播放器 · 账号登录"
                    color: theme ? theme.textMutedColor : "#7f8a9c"
                    font.pixelSize: 13
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                }
            }

            // 错误信息横幅
            Rectangle {
                id: errorBanner
                Layout.fillWidth: true
                implicitHeight: errorText.implicitHeight + 16
                radius: theme ? theme.controlRadius : 8
                color: "#2a1515"
                border.color: theme ? theme.dangerColor : "#e06c63"
                border.width: 1
                visible: root.authController && root.authController.loginError.length > 0

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text {
                        text: "\uE783"
                        font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                        color: theme ? theme.dangerColor : "#e06c63"
                        font.pixelSize: 14
                    }

                    Text {
                        id: errorText
                        Layout.fillWidth: true
                        text: root.authController ? root.authController.loginError : ""
                        color: theme ? theme.dangerColor : "#e06c63"
                        font.pixelSize: 12
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        wrapMode: Text.Wrap
                    }
                }
            }

            // 邮箱输入框
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "账号 / 邮箱"
                    color: theme ? theme.textSecondaryColor : "#b9c2d0"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 42
                    radius: theme ? theme.controlRadius : 8
                    color: theme ? theme.surfaceColor : "#161719"
                    border.color: emailInput.activeFocus ? (theme ? theme.accentColor : "#10b981") : (theme ? theme.borderColor : "#32343a")
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8

                        Text {
                            text: "\uE715"
                            font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                            color: theme ? theme.textMutedColor : "#7f8a9c"
                            font.pixelSize: 14
                        }

                        TextInput {
                            id: emailInput
                            Layout.fillWidth: true
                            color: theme ? theme.textPrimaryColor : "#f4f7fb"
                            font.pixelSize: 13
                            font.family: theme ? theme.fontFamily : "Segoe UI"
                            text: root.authController ? root.authController.savedEmail : ""
                            selectByMouse: true
                            verticalAlignment: TextInput.AlignVCenter

                            Text {
                                text: "请输入注册邮箱"
                                color: theme ? theme.textMutedColor : "#7f8a9c"
                                font.pixelSize: 13
                                font.family: theme ? theme.fontFamily : "Segoe UI"
                                visible: !emailInput.text && !emailInput.activeFocus
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Keys.onReturnPressed: passwordInput.forceActiveFocus()
                        }
                    }
                }
            }

            // 密码输入框
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "密码"
                    color: theme ? theme.textSecondaryColor : "#b9c2d0"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 42
                    radius: theme ? theme.controlRadius : 8
                    color: theme ? theme.surfaceColor : "#161719"
                    border.color: passwordInput.activeFocus ? (theme ? theme.accentColor : "#10b981") : (theme ? theme.borderColor : "#32343a")
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8

                        Text {
                            text: "\uE72E"
                            font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                            color: theme ? theme.textMutedColor : "#7f8a9c"
                            font.pixelSize: 14
                        }

                        TextInput {
                            id: passwordInput
                            Layout.fillWidth: true
                            color: theme ? theme.textPrimaryColor : "#f4f7fb"
                            font.pixelSize: 13
                            font.family: theme ? theme.fontFamily : "Segoe UI"
                            echoMode: card.showPassword ? TextInput.Normal : TextInput.Password
                            text: root.authController ? root.authController.savedPassword : ""
                            selectByMouse: true
                            verticalAlignment: TextInput.AlignVCenter

                            Text {
                                text: "请输入密码"
                                color: theme ? theme.textMutedColor : "#7f8a9c"
                                font.pixelSize: 13
                                font.family: theme ? theme.fontFamily : "Segoe UI"
                                visible: !passwordInput.text && !passwordInput.activeFocus
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Keys.onReturnPressed: root.triggerLogin()
                        }

                        // 显示/隐藏密码切换按钮
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 6
                            color: eyeMouse.containsMouse ? (theme ? theme.panelRaisedColor : "#2b2c30") : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: card.showPassword ? "\uE7B3" : "\uED1A"
                                font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                                color: theme ? theme.textSecondaryColor : "#b9c2d0"
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: eyeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: card.showPassword = !card.showPassword
                            }
                        }
                    }
                }
            }

            // 记住密码选项
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    width: 18
                    height: 18
                    radius: 4
                    color: rememberCheckbox.checked ? (theme ? theme.accentColor : "#10b981") : (theme ? theme.surfaceColor : "#161719")
                    border.color: theme ? theme.borderColor : "#32343a"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "\uE73E"
                        font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons, Segoe MDL2 Assets"
                        color: "#141517"
                        font.pixelSize: 10
                        font.bold: true
                        visible: rememberCheckbox.checked
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rememberCheckbox.checked = !rememberCheckbox.checked
                    }
                }

                Text {
                    id: rememberCheckbox
                    property bool checked: root.authController ? root.authController.rememberPassword : true
                    text: "记住账号与密码"
                    color: theme ? theme.textSecondaryColor : "#b9c2d0"
                    font.pixelSize: 12
                    font.family: theme ? theme.fontFamily : "Segoe UI"

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rememberCheckbox.checked = !rememberCheckbox.checked
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // 登录按钮
            Rectangle {
                id: loginButton
                Layout.fillWidth: true
                Layout.topMargin: 4
                implicitHeight: 44
                radius: theme ? theme.controlRadius : 8

                readonly property bool canSubmit: emailInput.text.trim().length > 0
                    && passwordInput.text.length > 0
                    && (!root.authController || !root.authController.isLoggingIn)

                color: !canSubmit
                    ? (theme ? theme.panelRaisedColor : "#2b2c30")
                    : (loginBtnMouse.pressed
                        ? Qt.darker(theme ? theme.accentColor : "#10b981", 1.15)
                        : (loginBtnMouse.containsMouse
                            ? Qt.lighter(theme ? theme.accentColor : "#10b981", 1.1)
                            : (theme ? theme.accentColor : "#10b981")))

                scale: loginBtnMouse.pressed ? 0.98 : 1.0
                Behavior on scale { NumberAnimation { duration: 100 } }
                Behavior on color { ColorAnimation { duration: 150 } }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8

                    BusyIndicator {
                        running: root.authController && root.authController.isLoggingIn
                        visible: running
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                    }

                    Text {
                        text: root.authController && root.authController.isLoggingIn ? "正在登录..." : "登  录"
                        color: !loginButton.canSubmit
                            ? (theme ? theme.textMutedColor : "#6b7280")
                            : (theme ? (root.authController && root.authController.isLoggingIn ? theme.textPrimaryColor : "#141517") : "#141517")
                        font.pixelSize: 15
                        font.bold: true
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                    }
                }

                MouseArea {
                    id: loginBtnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: loginButton.canSubmit ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.triggerLogin()
                }
            }
        }
    }

    function triggerLogin() {
        if (!authController) return
        if (authController.isLoggingIn) return
        const email = emailInput.text.trim()
        const pwd = passwordInput.text
        if (email.length === 0) {
            emailInput.forceActiveFocus()
            return
        }
        if (pwd.length === 0) {
            passwordInput.forceActiveFocus()
            return
        }
        authController.login(email, pwd, rememberCheckbox.checked)
    }
}
