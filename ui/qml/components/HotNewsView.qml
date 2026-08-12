import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    property var strings
    signal saveUiSettingRequested(string key, var value)
    property string selectedHotType: "baidu"
    onSelectedHotTypeChanged: root.saveUiSettingRequested("selectedHotType", selectedHotType)
    readonly property var hotTypes: [
        { label: "鐧惧害", value: "baidu" },
        { label: "璐村惂", value: "tieba" },
        { label: "鐭ヤ箮", value: "zhihu" },
        { label: "寰崥", value: "weibo" },
        { label: "鎶栭煶", value: "douyin" },
        { label: "B站", value: "bilihot" },
        { label: "少数派", value: "sspai" },
        { label: "鍏ㄧ珯", value: "biliall" },
        { label: "鍘嗗彶", value: "history" }
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
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: "鐑"
                            color: root.theme.textPrimaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.sectionTitleSize
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "聚合百度、贴吧、知乎、微博、抖音、B站、少数派等热搜榜单。"
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 34
                        radius: root.theme.controlRadius
                        color: hotRefreshHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor
                        opacity: root.controller.hotNewsLoading ? 0.55 : 1.0

                        HoverHandler { id: hotRefreshHover }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.controller.hotNewsLoading
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.controller.loadHotNews(root.selectedHotType)
                        }

                        Text {
                            anchors.centerIn: parent
                            text: root.strings.refreshLabel
                            color: "#ffffff"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            font.bold: true
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    layoutDirection: Qt.LeftToRight

                    Repeater {
                        model: root.hotTypes

                        Rectangle {
                            width: Math.max(64, hotTypeText.implicitWidth + 24)
                            height: 30
                            radius: 15
                            color: root.selectedHotType === modelData.value
                                ? root.theme.accentMutedColor
                                : (hotTypeHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor)
                            border.color: root.selectedHotType === modelData.value ? root.theme.accentColor : root.theme.borderColor
                            border.width: 1

                            HoverHandler { id: hotTypeHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.selectedHotType = modelData.value
                                    root.controller.loadHotNews(modelData.value)
                                }
                            }

                            Text {
                                id: hotTypeText
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.selectedHotType === modelData.value ? root.theme.accentColor : root.theme.textSecondaryColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.captionSize
                                font.bold: root.selectedHotType === modelData.value
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: root.theme.panelRadius
                    color: root.theme.surfaceColor
                    border.color: root.theme.subtleBorderColor
                    border.width: 1
                    clip: true

                    ListView {
                        id: hotNewsList
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        spacing: 8
                        model: root.controller.hotNewsItems

                        delegate: Rectangle {
                            readonly property bool hasTitle: (modelData.title || "").trim().length > 0
                            width: hotNewsList.width
                            height: hasTitle ? 64 : 0
                            visible: hasTitle
                            radius: root.theme.controlRadius
                            color: hotItemHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor
                            border.color: hotItemHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor
                            border.width: 1

                            HoverHandler { id: hotItemHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: modelData.url && modelData.url.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (modelData.url && modelData.url.length > 0) {
                                        Qt.openUrlExternally(modelData.url)
                                    }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 36
                                    Layout.preferredHeight: 36
                                    radius: 12
                                    color: modelData.rank <= 3 ? root.theme.accentMutedColor : root.theme.surfaceColor
                                    border.color: modelData.rank <= 3 ? root.theme.accentColor : root.theme.borderColor
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.rank
                                        color: modelData.rank <= 3 ? root.theme.accentColor : root.theme.textSecondaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.bodySize
                                        font.bold: true
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.title || ""
                                        color: root.theme.textPrimaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.bodySize
                                        font.bold: modelData.rank <= 3
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: modelData.hot && modelData.hot.length > 0
                                        text: modelData.hot
                                        color: root.theme.textMutedColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.captionSize
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    visible: modelData.url && modelData.url.length > 0
                                    text: "鎵撳紑"
                                    color: root.theme.accentColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                    font.bold: true
                                }
                            }
                        }
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: root.controller.hotNewsLoading
                        visible: running
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width * 0.72
                        visible: root.controller.hotNewsItems.length === 0 && !root.controller.hotNewsLoading
                        text: "点击刷新或切换类型加载热讯榜。"
                        color: root.theme.textMutedColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.controller.hotNewsMessage.length > 0
                    text: root.controller.hotNewsMessage
                    color: root.controller.hotNewsMessage.indexOf("澶辫触") >= 0 || root.controller.hotNewsMessage.indexOf("娌℃湁") >= 0
                        ? root.theme.dangerColor
                        : root.theme.accentColor
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.captionSize
                    elide: Text.ElideRight
                }
            }
        }
    }

    Component.onCompleted: {
        root.selectedHotType = root.controller.uiSetting("selectedHotType", root.selectedHotType)
        if (root.controller.hotNewsItems.length === 0) {
            root.controller.loadHotNews(root.selectedHotType)
        }
    }
}
