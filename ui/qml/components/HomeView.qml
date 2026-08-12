import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var controller
    required property var theme
    property int selectedHotTypeIndex: 0
    readonly property int memeSourceCount: 2
    readonly property int shortVideoSourceCount: 22
    readonly property int voiceSourceCount: 5
    readonly property var hotTypes: [
        { label: "百度", value: "baidu" },
        { label: "微博", value: "weibo" },
        { label: "知乎", value: "zhihu" },
        { label: "哔哩哔哩", value: "bilibili" },
        { label: "抖音", value: "douyin" },
        { label: "今日头条", value: "toutiao" }
    ]

    signal navigateRequested(int pageIndex)

    function siteCount(typeName) {
        var count = 0
        for (var i = 0; i < root.controller.apiSiteModel.count; ++i) {
            if (root.controller.apiSiteModel.typeAt(i) === typeName)
                count++
        }
        return count
    }

    function summaryCards() {
        return [
            { label: "功能模块", value: moduleCards().length },
            { label: "总站点", value: root.controller.apiSiteModel.count },
            { label: "视频站点", value: siteCount("video") },
            { label: "图片站点", value: siteCount("image") }
        ]
    }

    function moduleCards() {
        return [
            { title: "视频", desc: "搜索片源、查看详情并选集播放", count: siteCount("video"), unit: "站点", page: 1 },
            { title: "图片", desc: "浏览随机图片并管理图片来源", count: siteCount("image"), unit: "站点", page: 4 },
            { title: "短视频", desc: "连续播放短视频并快速切换来源", count: shortVideoSourceCount, unit: "来源", page: 6 },
            { title: "音乐", desc: "搜索歌曲、管理队列并查看歌词", count: root.controller.musicResults.length, unit: "曲目", page: 10 },
            { title: "下载", desc: "管理视频和音频下载任务", count: root.controller.downloadModel.count, unit: "任务", page: 3 },
            { title: "历史", desc: "继续播放最近打开的媒体内容", count: root.controller.historyModel.count, unit: "记录", page: 2 },
            { title: "表情包", desc: "搜索、预览和保存表情图片", count: memeSourceCount, unit: "接口", page: 5 },
            { title: "站点管理", desc: "检测、筛选并维护媒体 API 站点", count: root.controller.apiSiteModel.count, unit: "站点", page: 9 }
        ]
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.theme.edgePadding
        spacing: 14

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 126
                radius: root.theme.panelRadius
                color: root.theme.panelColor
                border.color: root.theme.subtleBorderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: "MeloBox 控制台"
                            color: root.theme.textPrimaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: 26
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "常用入口、资源站状态和实时热讯集中显示。"
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 210
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: "MeloBox " + root.controller.appVersion
                            color: root.theme.textSecondaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                            horizontalAlignment: Text.AlignRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            Button {
                                text: root.controller.updateChecking ? "检查中" : "检查更新"
                                enabled: !root.controller.updateChecking && !root.controller.updateDownloading
                                onClicked: root.controller.checkForUpdates()
                            }
                            Button {
                                text: root.controller.updateDownloading ? "下载中" : "安装更新"
                                enabled: root.controller.updateAvailable
                                    && !root.controller.updateChecking
                                    && !root.controller.updateDownloading
                                onClicked: root.controller.downloadAndInstallUpdate()
                            }
                        }
                    }
                }
            }

            GridView {
                id: summaryGrid
                Layout.fillWidth: true
                Layout.preferredHeight: 86
                interactive: false
                model: root.summaryCards()
                property int columnCount: 4
                cellWidth: width / columnCount
                cellHeight: 86

                delegate: Rectangle {
                    required property var modelData
                    x: 4
                    y: 3
                    width: summaryGrid.cellWidth - 8
                    height: 80
                    radius: root.theme.controlRadius
                    color: root.theme.surfaceColor
                    border.color: root.theme.subtleBorderColor
                    border.width: 1
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.value
                            color: root.theme.accentColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: 24
                            font.bold: true
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.label
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                        }
                    }
                }
            }

            GridView {
                id: moduleGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.moduleCards()
                property int columnCount: Math.max(1, Math.floor(width / 290))
                cellWidth: width / columnCount
                cellHeight: 132

                delegate: Rectangle {
                    required property var modelData
                    x: 4
                    y: 4
                    width: moduleGrid.cellWidth - 8
                    height: moduleGrid.cellHeight - 8
                    radius: root.theme.panelRadius
                    color: moduleHover.hovered ? root.theme.panelRaisedColor : root.theme.panelColor
                    border.color: moduleHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor
                    border.width: 1

                    HoverHandler { id: moduleHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.navigateRequested(modelData.page)
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: root.theme.textPrimaryColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: 17
                                font.bold: true
                            }
                            Text {
                                text: modelData.count + " " + modelData.unit
                                color: root.theme.accentColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.desc
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            text: "打开"
                            color: moduleHover.hovered ? root.theme.accentColor : root.theme.textSecondaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                            font.bold: true
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: Math.min(360, Math.max(300, root.width * 0.27))
            Layout.fillHeight: true
            visible: root.width >= 940
            radius: root.theme.panelRadius
            color: root.theme.panelColor
            border.color: root.theme.subtleBorderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: "实时热讯"
                            color: root.theme.textPrimaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: 20
                            font.bold: true
                        }
                        Text {
                            text: "聚合热榜和实时资讯"
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                        }
                    }
                    Button {
                        text: "刷新"
                        enabled: !root.controller.hotNewsLoading
                        onClicked: root.controller.loadHotNews(root.hotTypes[root.selectedHotTypeIndex].value)
                    }
                }

                ComboBox {
                    Layout.fillWidth: true
                    model: root.hotTypes
                    textRole: "label"
                    currentIndex: root.selectedHotTypeIndex
                    onActivated: function(index) {
                        root.selectedHotTypeIndex = index
                        root.controller.loadHotNews(root.hotTypes[index].value)
                    }
                }

                ListView {
                    id: hotNewsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 7
                    model: root.controller.hotNewsItems

                    delegate: Rectangle {
                        required property var modelData
                        width: hotNewsList.width
                        height: 58
                        radius: root.theme.controlRadius
                        color: hotItemHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor
                        border.color: hotItemHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor
                        border.width: 1
                        HoverHandler { id: hotItemHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: modelData.url && modelData.url.length > 0
                                ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (modelData.url && modelData.url.length > 0)
                                Qt.openUrlExternally(modelData.url)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 8
                            Text {
                                Layout.preferredWidth: 26
                                text: modelData.rank || ""
                                color: modelData.rank <= 3 ? root.theme.accentColor : root.theme.textMutedColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.bodySize
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title || ""
                                    color: root.theme.textPrimaryColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.hot || ""
                                    color: root.theme.textMutedColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.controller.hotNewsItems.length === 0
                            && !root.controller.hotNewsLoading
                        text: "点击刷新加载热讯"
                        color: root.theme.textMutedColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: root.controller.hotNewsLoading
                        visible: running
                    }
                }
            }
        }
    }
}
