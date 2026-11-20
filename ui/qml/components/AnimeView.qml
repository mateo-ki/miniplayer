import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ***(Dmghg)页面。
// 接口走加密直连网关,与通用 CMS 站点不同,因此独立页面。
// 流程: 搜索 → 结果网格 → 详情 → 选集 → episodeResolved → 播放。
Rectangle {
    id: root
    color: "transparent"
    anchors.fill: parent
    anchors.margins: 16
    property var controller
    property var theme
    property var sidebar: null

    AppStrings { id: appText }

    property bool detailActive: false
    property int selectedVodId: 0
    readonly property int episodesPerPage: 50
    property int currentEpisodePage: 0
    property string selectedType: ""
    property int selectedChannel: 0
    // 新协议的两个下拉是真数据：频道 = type/listActive 的顶层分类（按下标选，
    // 分类 id 是不透明雪花号），类型 = filter/dimensions 的 category 维度（按名字
    // 换 optionId）。旧协议没有对应接口，继续沿用下面这两张写死的表。
    readonly property var v3Channels: root.activeModel.channelOptions || []
    readonly property var v3Types: root.activeModel.typeOptions || []
    readonly property var legacyTypeNames: ["全部类型", "玄幻", "奇幻", "冒险", "猎奇", "纯爱", "续作", "机战",
        "魔法少女", "经典", "机甲", "励志", "超能力", "致郁", "乙女向", "美食",
        "漫画改", "搞笑", "轻小说改", "恋爱", "OVA", "百合", "校园", "战斗",
        "治愈", "日常", "后宫", "科幻", "异世界", "热血", "游戏改", "童年",
        "青春", "悬疑", "小说改", "音乐", "偶像", "推理", "穿越", "吐槽",
        "运动", "战争", "魔法", "职场", "剧情", "历史", "耽美", "轮回", "爱情"]
    // 新协议第 0 项是"全部频道"（列表不传 categoryId = 全站），具体频道要等 listActive。
    readonly property var channelNames: v3Channels.length
        ? ["全部频道"].concat(v3Channels.map(function (o) { return o.name; }))
        : ["全部频道", "日漫", "国漫", "剧场", "美漫", "特摄", "少儿", "动态漫"]
    readonly property var typeNames: v3Types.length
        ? ["全部类型"].concat(v3Types.map(function (o) { return o.name; }))
        : legacyTypeNames
    readonly property var model: controller ? services.dmghgAnimeModel : null
    // 控制器注入前页面可能先完成一轮绑定，使用空模型避免状态栏/详情绑定访问 null。
    readonly property var emptyModel: ({
        loading: false, totalCount: 0, count: 0, currentPage: 1, totalPages: 1,
        errorMessage: "", detailLoading: false, detail: ({}), episodes: [],
        currentSource: 0, commentsLoading: false, comments: [],
        useNewApi: false, channelOptions: [], typeOptions: [],
        scheduleLoading: false, scheduleDays: ({}),
        loadList: function() {}, search: function() {}, loadDetail: function() {},
        playEpisode: function() {}, loadComments: function() {},
        loadChannelOptions: function() {}, loadTypeOptions: function() {},
        loadWeeklySchedule: function() {}
    })
    readonly property var activeModel: root.model || root.emptyModel
    signal episodeRequested(string part)

    readonly property var weekdays: ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
    function getTodayWeekday() {
        var d = new Date()
        var day = d.getDay()
        return day === 0 ? 7 : day
    }
    property int selectedScheduleDay: getTodayWeekday()
    readonly property var scheduleDaysMap: root.activeModel.scheduleDays || ({})
    readonly property var currentScheduleItems: scheduleDaysMap[String(selectedScheduleDay)] || []
    property bool scheduleExpanded: true

    // 控制器通常在组件创建后才注入，所以注入/可见/成为当前页三处都可能触发首屏加载。
    // 三条信号在同一轮里几乎同时到达，而 loadList 是 callLater 延迟的，只靠
    // count/loading 判重会重复发同一个 51KB 请求，故再加一个"已排队"标记。
    property bool initialLoadQueued: false
    function requestInitialLoad() {
        if (initialLoadQueued || !model || model.count > 0 || model.loading) return
        initialLoadQueued = true
        Qt.callLater(function() {
            root.initialLoadQueued = false
            root.model.loadList(1, root.selectedType, root.selectedChannel)
            if (root.model.loadWeeklySchedule)
                root.model.loadWeeklySchedule()
        })
    }

    onModelChanged: requestInitialLoad()

    onVisibleChanged: if (visible && !detailActive) requestInitialLoad()

    StackLayout.onIsCurrentItemChanged: if (StackLayout.isCurrentItem && !detailActive) requestInitialLoad()

    function currentEpisodes() {
        return root.activeModel.episodes || []
    }

    function episodePageCount() {
        return Math.ceil(root.currentEpisodes().length / root.episodesPerPage)
    }

    function episodesOnCurrentPage() {
        var episodes = root.currentEpisodes()
        var start = root.currentEpisodePage * root.episodesPerPage
        return episodes.slice(start, Math.min(start + root.episodesPerPage, episodes.length))
    }

    StackLayout {
        anchors.fill: parent
        anchors.margins: 8
        currentIndex: root.detailActive ? 1 : 0

        // ── Page 0: 搜索结果 ─────────────────────────────
        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            // ── 每周更新排播卡片（对标 D:\project\rust\pool） ─────────────────────────────
            Rectangle {
                id: scheduleCard
                Layout.fillWidth: true
                Layout.preferredHeight: root.scheduleExpanded ? 210 : 44
                radius: theme.controlRadius
                color: theme.panelColor
                border.color: theme.borderColor
                border.width: 1
                clip: true

                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    // 头部：标题与周几切换按钮
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        spacing: 8

                        Text {
                            text: "每周更新"
                            color: theme.textPrimaryColor
                            font.family: theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                        }

                        Item { Layout.preferredWidth: 6 }

                        Repeater {
                            model: root.weekdays
                            Rectangle {
                                readonly property int dayNum: index + 1
                                readonly property bool isSelected: root.selectedScheduleDay === dayNum
                                readonly property bool isToday: root.getTodayWeekday() === dayNum
                                Layout.preferredWidth: isToday ? 58 : 46
                                Layout.preferredHeight: 26
                                radius: 4
                                color: isSelected
                                    ? theme.accentColor
                                    : (dayMouse.containsMouse ? theme.panelRaisedColor : theme.surfaceColor)
                                border.color: isSelected ? theme.accentColor : theme.borderColor
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData + (isToday ? "·今" : "")
                                    color: isSelected ? "#ffffff" : (dayMouse.containsMouse ? theme.textPrimaryColor : theme.textSecondaryColor)
                                    font.pixelSize: 11
                                    font.bold: isSelected || isToday
                                    font.family: theme.fontFamily
                                }

                                MouseArea {
                                    id: dayMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectedScheduleDay = dayNum
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // 折叠/展开按钮
                        Rectangle {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            radius: 4
                            color: expandMouse.containsMouse ? theme.panelRaisedColor : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: root.scheduleExpanded ? "▲" : "▼"
                                color: theme.textMutedColor
                                font.pixelSize: 10
                            }
                            MouseArea {
                                id: expandMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.scheduleExpanded = !root.scheduleExpanded
                            }
                        }
                    }

                    // 排播列表 (横向滑动)
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.scheduleExpanded

                        ListView {
                            id: scheduleList
                            anchors.fill: parent
                            orientation: ListView.Horizontal
                            spacing: 10
                            clip: true
                            model: root.currentScheduleItems

                            delegate: Rectangle {
                                width: 104
                                height: scheduleList.height
                                radius: 6
                                color: cardMouse.containsMouse ? theme.panelRaisedColor : "transparent"
                                border.color: cardMouse.containsMouse ? theme.accentColor : "transparent"
                                border.width: 1

                                MouseArea {
                                    id: cardMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        controller.resetMouseCursor()
                                        root.selectedVodId = modelData.vodId || 0
                                        root.detailActive = true
                                        root.activeModel.loadDetail(root.selectedVodId)
                                    }
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    spacing: 4

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 4
                                        clip: true
                                        color: theme.surfaceColor

                                        Image {
                                            anchors.fill: parent
                                            source: modelData.vodPic || ""
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            text: "🎬"
                                            font.pixelSize: 22
                                            color: theme.textMutedColor
                                            visible: !modelData.vodPic
                                        }

                                        // 底部更新时间标签
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            height: 18
                                            color: "#b0000000"

                                            Text {
                                                anchors.centerIn: parent
                                                width: parent.width - 6
                                                text: {
                                                    var s = modelData.schedule || ""
                                                    if (s.indexOf('|') >= 0) return s.split('|')[1]
                                                    return s || modelData.vodRemarks || ""
                                                }
                                                color: "#ffffff"
                                                font.pixelSize: 10
                                                font.bold: true
                                                elide: Text.ElideRight
                                                horizontalAlignment: Text.AlignHCenter
                                            }
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 18
                                        text: modelData.vodName || ""
                                        color: cardMouse.containsMouse ? theme.accentColor : theme.textPrimaryColor
                                        font.pixelSize: 11
                                        font.bold: true
                                        font.family: theme.fontFamily
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }

                        // 空状态提示
                        Text {
                            anchors.centerIn: parent
                            visible: root.currentScheduleItems.length === 0
                            text: root.activeModel.scheduleLoading
                                ? "正在加载每周更新排播表..."
                                : (root.weekdays[root.selectedScheduleDay - 1] + "暂时没有更新的连载热播动漫")
                            color: theme.textMutedColor
                            font.pixelSize: 12
                            font.family: theme.fontFamily
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                radius: theme.controlRadius
                color: theme.panelColor
                border.color: theme.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    ColumnLayout {
                        Layout.preferredWidth: 140
                        spacing: 0
                        Text {
                            text: "动漫"
                            color: theme.textPrimaryColor
                            font.family: theme.fontFamily
                            font.pixelSize: theme.titleSize
                            font.bold: true
                        }
                        Text {
                            text: ""
                            visible: false
                            color: theme.textMutedColor
                            font.family: theme.fontFamily
                            font.pixelSize: theme.captionSize
                        }
                    }

                    TextField {
                        id: searchInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        placeholderText: "搜索动漫,例如:择日飞升"
                        color: theme.textPrimaryColor
                        font.pixelSize: 13
                        font.family: theme.fontFamily
                        background: Rectangle {
                            radius: 6
                            color: theme.surfaceColor
                            border.color: searchInput.activeFocus ? theme.accentColor : theme.borderColor
                            border.width: 1
                        }
                        onAccepted: doSearch()
                    }

                    ComboBox {
                        id: channelFilter
                        Layout.preferredWidth: 104
                        Layout.preferredHeight: 32
                        model: root.channelNames
                        onPressedChanged: if (pressed) root.activeModel.loadChannelOptions()
                        // 旧协议要传真实 channel 数值；新协议传下拉下标（分类 id 在客户端里查）。
                        property var channelIds: [0, 20, 21, 22, 23, 25, 32, 26]
                        currentIndex: 0
                        onActivated: {
                            root.selectedChannel = root.v3Channels.length
                                ? currentIndex : channelIds[currentIndex]
                            if (searchInput.text.trim().length === 0)
                                root.activeModel.loadList(1, root.selectedType, root.selectedChannel)
                        }
                    }

                    ComboBox {
                        id: typeFilter
                        Layout.preferredWidth: 112
                        Layout.preferredHeight: 32
                        model: root.typeNames
                        currentIndex: 0
                        onPressedChanged: if (pressed) root.activeModel.loadTypeOptions()
                        onActivated: {
                            root.selectedType = currentIndex === 0 ? "" : currentText
                            if (searchInput.text.trim().length === 0)
                                 root.activeModel.loadList(1, root.selectedType, root.selectedChannel)
                        }
                    }

                    ComboBox {
                        id: protocolFilter
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 32
                        // 协议切换: 新算法(/api/v1/app/client/*, magic_sdk 信封+md5 签名)
                        // 与 旧算法(pc/* 时间桶 IV+RSA)。两套 id 体系不同, 切换即重载列表。
                        model: ["新算法", "旧算法"]
                        currentIndex: root.activeModel.useNewApi === true ? 0 : 1
                        onActivated: {
                            if (!root.activeModel.setUseNewApi)
                                return
                            root.activeModel.setUseNewApi(currentIndex === 0)
                            root.selectedChannel = 0
                            root.selectedType = ""
                            channelFilter.currentIndex = 0
                            typeFilter.currentIndex = 0
                            // 两套协议的频道/类型来源不同，切完重取一份。
                            root.activeModel.loadChannelOptions()
                            if (searchInput.text.trim().length === 0)
                                root.activeModel.loadList(1, "", 0)
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 32
                        radius: 6
                        color: searchBtnMouse.containsMouse ? theme.accentMutedColor : theme.accentColor

                        MouseArea {
                            id: searchBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: doSearch()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "搜索"
                            color: "#ffffff"
                            font.pixelSize: 13
                            font.bold: true
                            font.family: theme.fontFamily
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 32
                        radius: 6
                        color: listBtnMouse.containsMouse ? theme.panelRaisedColor : theme.surfaceColor
                        border.color: listBtnMouse.containsMouse ? theme.accentColor : theme.borderColor
                        border.width: 1

                        MouseArea {
                            id: listBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                searchInput.text = ""
                                typeFilter.currentIndex = 0
                                root.selectedType = ""
                                 root.activeModel.loadList(1, "", root.selectedChannel)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "列表"
                            color: listBtnMouse.containsMouse ? theme.accentColor : theme.textSecondaryColor
                            font.pixelSize: 13
                            font.family: theme.fontFamily
                        }
                    }
                }
            }

            // 状态栏
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4

                    Text {
                        text: root.activeModel.loading ? "加载中..." :
                              (root.activeModel.totalCount > 0 ? "共 " + root.activeModel.totalCount + " 条结果" :
                               (root.activeModel.errorMessage ? "错误: " + root.activeModel.errorMessage : ""))
                        color: root.activeModel.errorMessage ? theme.dangerColor : theme.textMutedColor
                        font.pixelSize: 11
                        font.family: theme.fontFamily
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            // 结果网格
            GridView {
                id: resultGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                cellWidth: 200
                cellHeight: 300
                clip: true
                model: root.activeModel

                delegate: Rectangle {
                    width: resultGrid.cellWidth - 8
                    height: resultGrid.cellHeight - 8
                    radius: theme.controlRadius
                    color: detailMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor
                    border.color: detailMouse.containsMouse ? theme.accentColor : theme.borderColor
                    border.width: 1

                    MouseArea {
                        id: detailMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            controller.resetMouseCursor()
                            root.selectedVodId = model.vodId || 0
                            root.detailActive = true
                            root.activeModel.loadDetail(root.selectedVodId)
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 220
                            radius: 6
                            color: theme.surfaceColor
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: model.vodPic || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                                    Math.ceil(height * Screen.devicePixelRatio))

                                BusyIndicator {
                                    anchors.centerIn: parent
                                    running: parent.status === Image.Loading
                                    visible: running
                                    width: 32
                                    height: 32
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: "\uE8B2"
                                    font.family: theme.iconFontFamily
                                    font.pixelSize: 40
                                    visible: parent.status === Image.Error || !model.vodPic
                                    color: theme.textMutedColor
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.vodName || ""
                            color: theme.textPrimaryColor
                            font.pixelSize: 12
                            font.bold: true
                            font.family: theme.fontFamily
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: {
                                var parts = []
                                if (model.vodYear) parts.push(model.vodYear)
                                if (model.vodArea) parts.push(model.vodArea)
                                if (model.typeName) parts.push(model.typeName)
                                return parts.join(" · ")
                            }
                            color: theme.textMutedColor
                            font.pixelSize: 10
                            font.family: theme.fontFamily
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        visible: model.vodRemarks && model.vodRemarks.length > 0
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 8
                        anchors.bottomMargin: 8
                        width: Math.min(remarksLabel.implicitWidth + 10, parent.width - 16)
                        height: 20
                        radius: 4
                        color: "#b31b1d22"
                        z: 5

                        Text {
                            id: remarksLabel
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            text: model.vodRemarks || ""
                            color: "#ffffff"
                            font.pixelSize: 10
                            font.family: theme.fontFamily
                            font.bold: true
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    visible: resultGrid.count === 0 && !root.activeModel.loading

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.activeModel.errorMessage && root.activeModel.errorMessage.length > 0
                            ? root.activeModel.errorMessage
                            : "输入关键词搜索动漫,或点击“列表”浏览"
                        color: root.activeModel.errorMessage && root.activeModel.errorMessage.length > 0 ? theme.dangerColor : theme.textMutedColor
                        font.pixelSize: 15
                        font.family: theme.fontFamily
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 96
                        height: 32
                        radius: 6
                        color: retryMouse.containsMouse ? theme.accentMutedColor : theme.accentColor

                        Text {
                            anchors.centerIn: parent
                            text: "重新加载"
                            color: "#ffffff"
                            font.pixelSize: 13
                            font.bold: true
                            font.family: theme.fontFamily
                        }

                        MouseArea {
                            id: retryMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (searchInput.text.trim().length > 0) {
                                    doSearch()
                                } else {
                                    root.activeModel.loadList(1, root.selectedType, root.selectedChannel)
                                }
                            }
                        }
                    }
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: root.activeModel.loading
                    visible: running
                    width: 48
                    height: 48
                }
            }

            // 分页栏固定在结果区域底部并水平居中
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 30 : 0
                spacing: 6
                visible: root.activeModel
                    && (root.activeModel.currentPage > 1
                        || root.activeModel.currentPage < root.activeModel.totalPages)

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 28
                    radius: 5
                    color: animePreviousMouse.enabled
                        ? (animePreviousMouse.containsMouse
                            ? theme.panelRaisedColor : theme.panelColor)
                        : theme.surfaceColor
                    border.color: animePreviousMouse.enabled
                        ? theme.borderColor : theme.subtleBorderColor
                    opacity: animePreviousMouse.enabled ? 1.0 : 0.55

                    Text {
                        anchors.centerIn: parent
                        text: "上一页"
                        color: theme.textSecondaryColor
                        font.pixelSize: 11
                        font.family: theme.fontFamily
                    }

                    MouseArea {
                        id: animePreviousMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.activeModel
                            && !root.activeModel.loading
                            && root.activeModel.currentPage > 1
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.loadPage(root.activeModel.currentPage - 1)
                    }
                }

                Text {
                    text: "第 " + root.activeModel.currentPage + " 页"
                    color: theme.textMutedColor
                    font.pixelSize: 11
                    font.family: theme.fontFamily
                }

                Rectangle {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 28
                    radius: 5
                    color: animeNextMouse.enabled
                        ? (animeNextMouse.containsMouse
                            ? theme.panelRaisedColor : theme.panelColor)
                        : theme.surfaceColor
                    border.color: animeNextMouse.enabled
                        ? theme.borderColor : theme.subtleBorderColor
                    opacity: animeNextMouse.enabled ? 1.0 : 0.55

                    Text {
                        anchors.centerIn: parent
                        text: "下一页"
                        color: theme.textSecondaryColor
                        font.pixelSize: 11
                        font.family: theme.fontFamily
                    }

                    MouseArea {
                        id: animeNextMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.activeModel
                            && !root.activeModel.loading
                            && root.activeModel.currentPage < root.activeModel.totalPages
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.loadPage(root.activeModel.currentPage + 1)
                    }
                }

                Item { Layout.fillWidth: true }
            }

        }

        // ── Page 1: 详情 + 选集 ──────────────────────────
        Item {
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 32
                        radius: 6
                        color: backMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor
                        border.color: backMouse.containsMouse ? theme.accentColor : theme.borderColor
                        border.width: 1

                        MouseArea {
                            id: backMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.detailActive = false
                        }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "\uE72B"
                                font.family: theme.iconFontFamily
                                font.pixelSize: 11
                                color: backMouse.containsMouse ? theme.accentColor : theme.textSecondaryColor
                            }

                            Text {
                                text: "返回"
                                color: backMouse.containsMouse ? theme.accentColor : theme.textSecondaryColor
                                font.pixelSize: 12
                                font.family: theme.fontFamily
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.activeModel.detail.vodName || "加载中..."
                        color: theme.textPrimaryColor
                        font.pixelSize: 16
                        font.bold: true
                        font.family: theme.fontFamily
                        elide: Text.ElideRight
                    }
                }

                Flickable {
                    id: detailScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: detailColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        width: 8
                        anchors.right: parent.right
                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: theme.accentColor
                            opacity: parent.pressed ? 0.95 : 0.65
                        }
                        background: Rectangle {
                            implicitWidth: 8
                            radius: 4
                            color: theme.panelColor
                            opacity: 0.35
                        }
                    }

                    ColumnLayout {
                        id: detailColumn
                        width: parent.width
                        spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 420
                spacing: 16

                // 海报
                Rectangle {
                    Layout.preferredWidth: Math.max(320, root.width * 0.5 - 8)
                    Layout.maximumWidth: Math.max(320, root.width * 0.5 - 8)
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    radius: 8
                    color: theme.surfaceColor
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: root.activeModel.detail.vodPic || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: parent.status === Image.Loading
                            visible: running
                            width: 32
                            height: 32
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "\uE8B2"
                            font.family: theme.iconFontFamily
                            font.pixelSize: 52
                            visible: parent.status === Image.Error || !root.activeModel.detail.vodPic
                            color: theme.textMutedColor
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: Math.max(320, root.width * 0.5 - 8)
                    Layout.minimumWidth: 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: {
                                var parts = []
                                var d = root.activeModel.detail
                                if (d.vodYear) parts.push(d.vodYear)
                                if (d.vodArea) parts.push(d.vodArea)
                                if (d.typeName) parts.push(d.typeName)
                                if (d.vodScore && d.vodScore !== "0.0") parts.push("评分 " + d.vodScore)
                                if (d.vodRemarks) parts.push(d.vodRemarks)
                                return parts
                            }

                            Rectangle {
                                radius: 4
                                color: theme.surfaceColor
                                border.color: theme.borderColor
                                border.width: 1
                                implicitWidth: metaLabel.implicitWidth + 12
                                implicitHeight: metaLabel.implicitHeight + 6

                                Text {
                                    id: metaLabel
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: theme.textSecondaryColor
                                    font.pixelSize: 11
                                    font.family: theme.fontFamily
                                }
                            }
                        }
                    }

                    Text {
                        visible: (root.activeModel.detail.vodDirector || "").length > 0
                        Layout.fillWidth: true
                        text: "导演: " + (root.activeModel.detail.vodDirector || "")
                        color: theme.textMutedColor
                        font.pixelSize: 14
                        font.family: theme.fontFamily
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: (root.activeModel.detail.vodActor || "").length > 0
                        Layout.fillWidth: true
                        text: appText.actorPrefix + (root.activeModel.detail.vodActor || "")
                        color: theme.textMutedColor
                        font.pixelSize: 14
                        font.family: theme.fontFamily
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "简介"
                        color: theme.textPrimaryColor
                        font.pixelSize: 16
                        font.bold: true
                        font.family: theme.fontFamily
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentHeight: descText.implicitHeight

                        Text {
                            id: descText
                            width: parent.width
                            text: (root.activeModel.detail.vodContent || "").replace(/<[^>]*>/g, '')
                            color: theme.textSecondaryColor
                            font.pixelSize: 16
                            font.family: theme.fontFamily
                            wrapMode: Text.Wrap
                            lineHeight: 1.35
                            lineHeightMode: Text.ProportionalHeight
                        }
                    }
                }
            }

            // 选集区
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                radius: 8
                color: theme.surfaceColor
                border.color: theme.borderColor
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Repeater {
                            model: root.activeModel.detail.playSources || []

                            Rectangle {
                                Layout.preferredWidth: sourceLabel.implicitWidth + 16
                                Layout.preferredHeight: 26
                                radius: 4
                                color: root.activeModel.currentSource === index
                                    ? theme.accentColor
                                    : (sourceMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor)

                                MouseArea {
                                    id: sourceMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        root.currentEpisodePage = 0
                                        root.activeModel.currentSource = index
                                    }
                                }

                                Text {
                                    id: sourceLabel
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: root.activeModel.currentSource === index ? "#ffffff" : theme.textSecondaryColor
                                    font.pixelSize: 11
                                    font.family: theme.fontFamily
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            visible: root.activeModel.detailLoading
                            text: "解析中..."
                            color: theme.accentColor
                            font.pixelSize: 11
                            font.family: theme.fontFamily
                        }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 28 : 0
                        visible: root.episodePageCount() > 1
                        clip: true
                        contentWidth: episodeRangeRow.width
                        contentHeight: height
                        boundsBehavior: Flickable.StopAtBounds

                        Row {
                            id: episodeRangeRow
                            height: parent.height
                            spacing: 4

                            Repeater {
                                model: root.episodePageCount()

                                Rectangle {
                                    width: rangeLabel.implicitWidth + 20
                                    height: 28
                                    radius: 4
                                    color: root.currentEpisodePage === index
                                        ? theme.accentColor
                                        : (rangeMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor)
                                    border.color: root.currentEpisodePage === index
                                        ? theme.accentColor : theme.borderColor
                                    border.width: 1

                                    MouseArea {
                                        id: rangeMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.currentEpisodePage = index
                                    }

                                    Text {
                                        id: rangeLabel
                                        anchors.centerIn: parent
                                        text: {
                                            var start = index * root.episodesPerPage + 1
                                            var end = Math.min((index + 1) * root.episodesPerPage,
                                                               root.currentEpisodes().length)
                                            return start + "-" + end
                                        }
                                        color: root.currentEpisodePage === index
                                            ? "#ffffff" : theme.textSecondaryColor
                                        font.pixelSize: 11
                                        font.family: theme.fontFamily
                                    }
                                }
                            }
                        }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: episodeFlow.height

                        Flow {
                            id: episodeFlow
                            width: parent.width
                            spacing: 4

                            Repeater {
                                model: root.episodesOnCurrentPage()

                                Rectangle {
                                    width: 72
                                    height: 28
                                    radius: 4
                                    readonly property bool isCurrent: (root.currentEpisodePage * root.episodesPerPage + index) === controller.currentIndex
                                    color: isCurrent
                                        ? theme.accentMutedColor
                                        : (epMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor)
                                    border.color: isCurrent
                                        ? theme.accentColor
                                        : (epMouse.containsMouse ? theme.accentColor : theme.borderColor)
                                    border.width: isCurrent ? 1.5 : 1

                                    MouseArea {
                                        id: epMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            var vid = root.activeModel.detail.vodId || root.selectedVodId
                                            if (vid <= 0) return
                                            controller.setCurrentVodName(root.activeModel.detail.vodName || "")
                                            root.episodeRequested(modelData)
                                            root.activeModel.playEpisode(vid, modelData)
                                        }
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: parent.isCurrent ? theme.accentColor : theme.textPrimaryColor
                                        font.pixelSize: 11
                                        font.bold: parent.isCurrent
                                        font.family: theme.fontFamily
                                        elide: Text.ElideRight
                                        width: parent.width - 8
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: commentsSectionColumn.implicitHeight + 28
                radius: 8
                color: theme.surfaceColor
                border.color: theme.borderColor
                border.width: 1

                ColumnLayout {
                    id: commentsSectionColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "评论"
                            color: theme.textPrimaryColor
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Text {
                            text: root.activeModel.commentsLoading ? "加载中..." : (root.activeModel.comments.length + " 条")
                            color: theme.textMutedColor
                            font.pixelSize: 11
                            Layout.leftMargin: 6
                        }
                        Item { Layout.fillWidth: true }
                        ToolButton {
                            contentItem: Text {
                                text: "\uE72C"
                                font.family: theme.iconFontFamily
                                font.pixelSize: 13
                                color: theme.textSecondaryColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "刷新评论"
                            onClicked: root.activeModel.loadComments(root.activeModel.detail.vodId || root.selectedVodId, 1)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: root.activeModel.comments
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: commentColumn.implicitHeight + 18
                                Layout.preferredHeight: implicitHeight
                                radius: 6
                                color: theme.panelColor
                                border.color: theme.subtleBorderColor
                                border.width: 1

                                ColumnLayout {
                                    id: commentColumn
                                    anchors.fill: parent
                                    anchors.margins: 9
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: modelData.uname || "匿名用户"
                                            color: theme.accentColor
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                        Text {
                                            visible: modelData.isTop
                                            text: "置顶"
                                            color: theme.warningColor
                                            font.pixelSize: 10
                                        }
                                        Item { Layout.fillWidth: true }
                                        Text { text: modelData.createdAt || ""; color: theme.textMutedColor; font.pixelSize: 10 }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.comments || ""
                                        color: theme.textSecondaryColor
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
            }
        }
    }
    }
    }

    Connections {
        target: root.model
        function onDetailChanged() {
            var vid = root.activeModel.detail.vodId || root.selectedVodId
            if (vid > 0) root.activeModel.loadComments(vid, 1)
        }
    }

    function doSearch() {
        var keyword = searchInput.text.trim()
        if (keyword.length === 0) {
            root.activeModel.loadList(1)
            return
        }
        root.activeModel.search(keyword, 1)
    }

    function loadPage(page) {
        var keyword = searchInput.text.trim()
        if (keyword.length === 0) {
        root.activeModel.loadList(page, root.selectedType, root.selectedChannel)
            return
        }
        root.activeModel.search(keyword, page)
    }

    Component.onCompleted: {
        // 默认"全部频道"不依赖任何字典接口；频道名(≈3KB)和类型维度(≈35KB)
        // 都等用户点开对应下拉再取，首屏因此只发一次列表请求。
        requestInitialLoad()
    }
}
