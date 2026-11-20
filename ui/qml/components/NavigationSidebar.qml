import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: 64
    color: theme ? theme.chromeColor : "#141517"

    property int currentIndex: 1
    // 播放器内容页可临时与导航高亮页分离，例如动漫播放仍高亮“动漫”。
    property int activePageOverride: -1
    function isKnownPage(pageIndex) {
        for (var i = 0; i < navItems.length; ++i) {
            if (navItems[i].page === pageIndex) return true;
        }
        for (var j = 0; j < bottomNavItems.length; ++j) {
            if (bottomNavItems[j].page === pageIndex) return true;
        }
        return false;
    }
    // 兜底防御：若 activePage 处于未知/已删除页码，自动回退到“视频(1)”，确保左侧菜单绝不会出现“全未选中”状态。
    readonly property int activePage: {
        var page = activePageOverride >= 0 ? activePageOverride : currentIndex
        return isKnownPage(page) ? page : 1
    }
    property var theme
    // 「我的」那一格该不该亮红点：数字由外面喂进来，这里只管画。
    // 亮不亮都不改行宽，红点是贴在图标右上角的浮层，不占布局。
    property int unreadBadge: 0
    // 有可用新版本时在「更新」那一格亮红点
    property bool updateAvailable: false
    signal logoClicked()
    readonly property int logoHeight: 56
    readonly property var navItems: [
        { icon: "\uE714", label: "视频", page: 1 },
        { icon: "\uE8B2", label: "动漫", page: 9 },
        { icon: "\uEC15", label: "短视频", page: 6 },
        { icon: "\uEB9F", label: "图片", page: 4 },
        { icon: "\uE76E", label: "表情", page: 5 },
        { icon: "\uE720", label: "语音", page: 7 }
    ]
    readonly property var bottomNavItems: [
        { icon: "\uE777", label: "更新", page: 3 },
        { icon: "\uE81C", label: "历史", page: 2 },
        { icon: "\uE779", label: "我的", page: 10 }
    ]
    readonly property real navButtonHeight: Math.max(50, Math.min(62, (height - logoHeight) / (navItems.length + bottomNavItems.length)))

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Logo area (clickable to view user profile)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.logoHeight
            color: "transparent"

            Rectangle {
                id: logoBox
                anchors.centerIn: parent
                width: 38
                height: 38
                radius: 10
                color: logoMouse.containsMouse ? (theme ? theme.panelRaisedColor : "#26262c") : (theme ? theme.surfaceColor : "#18181c")
                border.color: logoMouse.containsMouse ? (theme ? theme.accentColor : "#38bdf8") : (theme ? theme.borderColor : "#2a2a30")
                border.width: 1

                scale: logoMouse.pressed ? 0.94 : (logoMouse.containsMouse ? 1.06 : 1.0)
                Behavior on scale { NumberAnimation { duration: 120 } }
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 150 } }

                Text {
                    anchors.centerIn: parent
                    text: "M"
                    color: theme ? theme.accentColor : "#38bdf8"
                    font.pixelSize: 20
                    font.bold: true
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                }

                MouseArea {
                    id: logoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.logoClicked()
                }

                ToolTip.visible: logoMouse.containsMouse
                ToolTip.delay: 350
                ToolTip.text: "个人信息中心 (点击查看)"
            }
        }

        // Navigation buttons
        Repeater {
            model: root.navItems

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.navButtonHeight
                color: "transparent"

                MouseArea {
                    id: bottomMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.activePageOverride = -1
                        root.currentIndex = modelData.page
                    }
                }

                Rectangle {
                    width: 3
                    height: 28
                    radius: 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme ? theme.accentColor : "#38bdf8"
                    visible: root.activePage === modelData.page
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 48
                    height: 44
                    radius: 12
                    color: root.activePage === modelData.page
                        ? (theme ? theme.sidebarActiveColor : "#202026")
                        : (bottomMouseArea.containsMouse ? (theme ? theme.panelRaisedColor : "#26262c") : "transparent")
                    border.color: root.activePage === modelData.page ? (theme ? theme.borderColor : "#2a2a30") : "transparent"
                    border.width: 1
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.icon
                        font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons"
                        font.pixelSize: 18
                        renderType: Text.NativeRendering
                        color: root.activePage === modelData.page ? theme.accentColor : theme.textSecondaryColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        font.pixelSize: 10
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        color: root.activePage === modelData.page ? theme.accentColor : theme.textMutedColor
                    }
                }
            }
        }

        // Spacer
        Item { Layout.fillHeight: true }

        Repeater {
            model: root.bottomNavItems

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.navButtonHeight
                color: "transparent"

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.activePageOverride = -1
                        root.currentIndex = modelData.page
                    }
                }

                Rectangle {
                    width: 3
                    height: 28
                    radius: 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme ? theme.accentColor : "#38bdf8"
                    visible: root.activePage === modelData.page
                }

                Rectangle {
                    id: bottomNavBtnBox
                    anchors.centerIn: parent
                    width: 48
                    height: 44
                    radius: 12
                    color: root.activePage === modelData.page
                        ? (theme ? theme.sidebarActiveColor : "#202026")
                        : (mouseArea.containsMouse ? (theme ? theme.panelRaisedColor : "#26262c") : "transparent")
                    border.color: root.activePage === modelData.page ? (theme ? theme.borderColor : "#2a2a30") : "transparent"
                    border.width: 1
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        id: bottomNavIcon
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.icon
                        font.family: theme ? theme.iconFontFamily : "Segoe Fluent Icons"
                        font.pixelSize: 18
                        renderType: Text.NativeRendering
                        color: root.activePage === modelData.page ? theme.accentColor : theme.textSecondaryColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        font.pixelSize: 10
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        color: root.activePage === modelData.page ? theme.accentColor : theme.textMutedColor
                    }
                }

                // 贴在图标右上角的浮层：挂在 bottomNavBtnBox 边角，避免非法跨组件锚点
                Rectangle {
                    visible: (modelData.page === 10 && root.unreadBadge > 0)
                          || (modelData.page === 3 && root.updateAvailable)
                    width: 9
                    height: 9
                    radius: 5
                    anchors.right: bottomNavBtnBox.right
                    anchors.rightMargin: 8
                    anchors.top: bottomNavBtnBox.top
                    anchors.topMargin: 4
                    color: theme ? theme.dangerColor : "#ef4444"
                    border.color: theme ? theme.chromeColor : "#141517"
                    border.width: 1
                }
            }
        }
    }
}
