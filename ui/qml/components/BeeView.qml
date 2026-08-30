pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"

    property var controller
    property var theme
    property var sidebar
    readonly property var model: controller ? controller.beeVideoModel : null
    readonly property var schedule: controller ? controller.beeScheduleModel : null
    property string selectedId: ""
    property bool detailActive: false

    signal playbackRequested()

    function search() {
        if (!root.model)
            return
        root.detailActive = false
        root.model.search(searchInput.text.trim())
    }

    function detailValue(name) {
        if (!root.model || !root.model.detail)
            return ""
        var value = root.model.detail[name]
        return value === undefined || value === null ? "" : value
    }

    function ensureRecommended() {
        if (root.model && root.model.count === 0 && !root.model.loading)
            root.model.loadRecommended()
    }

    function ensureScheduleLoaded() {
        if (root.schedule && root.schedule.days.length === 0 && !root.schedule.loading)
            root.schedule.load()
    }

    StackLayout {
        anchors.fill: parent
        anchors.margins: root.theme.edgePadding
        currentIndex: root.detailActive ? 1 : 0

        // ── Page 0: 近期热播 + 追剧日历 (统一垂直滚动) ──
        Flickable {
            id: homeFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: homeColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: homeColumn
                width: homeFlickable.width
                spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                radius: root.theme.controlRadius
                color: root.theme.panelColor
                border.color: root.theme.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    ColumnLayout {
                        Layout.preferredWidth: root.width < 620 ? 82 : 150
                        Layout.maximumWidth: root.width < 620 ? 82 : 150
                        spacing: 1

                        Text {
                            text: "蜜蜂"
                            color: root.theme.textPrimaryColor
                            font.pixelSize: 15
                            font.bold: true
                            font.family: root.theme.fontFamily
                        }

                        Text {
                            text: "BeeWeb 点播"
                            visible: root.width >= 620
                            color: root.theme.textMutedColor
                            font.pixelSize: 10
                            font.family: root.theme.fontFamily
                        }
                    }

                    TextField {
                        id: searchInput
                        Layout.fillWidth: true
                        Layout.minimumWidth: 100
                        Layout.preferredWidth: 300
                        Layout.maximumWidth: 420
                        Layout.preferredHeight: 32
                        placeholderText: "搜索视频；留空显示近期热播"
                        color: root.theme.textPrimaryColor
                        font.pixelSize: 13
                        font.family: root.theme.fontFamily
                        background: Rectangle {
                            radius: 6
                            color: root.theme.surfaceColor
                            border.color: searchInput.activeFocus
                                ? root.theme.accentColor : root.theme.borderColor
                            border.width: 1
                        }
                        onAccepted: root.search()
                    }

                    Rectangle {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 32
                        radius: 6
                        color: searchButtonMouse.containsMouse
                            ? root.theme.accentMutedColor : root.theme.accentColor

                        MouseArea {
                            id: searchButtonMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.search()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: searchInput.text.trim().length > 0 ? "搜索" : "推荐"
                            color: "#ffffff"
                            font.pixelSize: 13
                            font.bold: true
                            font.family: root.theme.fontFamily
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.model && root.model.loading
                    ? "正在加载视频..."
                    : (root.model && root.model.errorMessage ? root.model.errorMessage : "")
                color: root.model && root.model.errorMessage
                    ? root.theme.dangerColor : root.theme.textMutedColor
                font.pixelSize: root.theme.captionSize
                font.family: root.theme.fontFamily
                visible: text.length > 0
            }

            GridView {
                id: resultGrid
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(
                    cellHeight,
                    Math.ceil(
                        (root.model ? root.model.count : 0)
                        / Math.max(1, Math.floor(width / cellWidth))
                    ) * cellHeight
                )
                cellWidth: 200
                cellHeight: 300
                clip: true
                interactive: false
                model: root.model

                delegate: Rectangle {
                    id: beeCard
                    required property string vodId
                    required property string vodName
                    required property string vodPic
                    required property string vodRemarks
                    required property string typeName
                    required property string vodYear
                    required property string vodArea
                    required property string vodClass
                    required property string vodBlurb
                    required property string vodVersion

                    width: resultGrid.cellWidth - 8
                    height: resultGrid.cellHeight - 8
                    radius: root.theme.controlRadius
                    color: beeMouse.containsMouse
                        ? root.theme.panelRaisedColor : root.theme.panelColor
                    border.color: beeMouse.containsMouse
                        ? root.theme.accentColor : root.theme.borderColor
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 210
                            radius: 6
                            color: root.theme.surfaceColor
                            clip: true

                            Image {
                                id: poster
                                anchors.fill: parent
                                source: beeCard.vodPic || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                                    Math.ceil(height * Screen.devicePixelRatio))
                            }

                            BusyIndicator {
                                anchors.centerIn: parent
                                running: poster.status === Image.Loading
                                visible: running
                                width: 30
                                height: 30
                            }

                            Text {
                                anchors.centerIn: parent
                                text: "影片"
                                color: root.theme.textMutedColor
                                font.pixelSize: 22
                                font.family: root.theme.fontFamily
                                visible: poster.status === Image.Error || !beeCard.vodPic
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: beeCard.vodName || "未命名"
                            color: root.theme.textPrimaryColor
                            font.pixelSize: 13
                            font.bold: true
                            font.family: root.theme.fontFamily
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: {
                                var parts = []
                                if (beeCard.vodYear) parts.push(beeCard.vodYear)
                                if (beeCard.vodClass) parts.push(beeCard.vodClass)
                                else if (beeCard.typeName) parts.push(beeCard.typeName)
                                if (beeCard.vodVersion) parts.push(beeCard.vodVersion)
                                return parts.join(" · ")
                            }
                            color: root.theme.textMutedColor
                            font.pixelSize: root.theme.captionSize
                            font.family: root.theme.fontFamily
                            elide: Text.ElideRight
                        }

                        Item { Layout.fillHeight: true }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: beeCard.vodRemarks || ""
                                color: root.theme.textMutedColor
                                font.pixelSize: root.theme.captionSize
                                font.family: root.theme.fontFamily
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "查看详情"
                                color: beeMouse.containsMouse
                                    ? root.theme.accentColor : root.theme.textSecondaryColor
                                font.pixelSize: root.theme.captionSize
                                font.family: root.theme.fontFamily
                            }
                        }
                    }

                    MouseArea {
                        id: beeMouse
                        anchors.fill: parent
                        z: 20
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.selectedId = beeCard.vodId
                            root.detailActive = true
                            root.model.loadDetail(beeCard.vodId)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: {
                        if (root.model && root.model.loading)
                            return ""
                        if (root.model && root.model.errorMessage)
                            return ""
                        return searchInput.text.trim().length > 0
                            ? "没有找到相关视频" : "暂无推荐视频"
                    }
                    color: root.theme.textMutedColor
                    font.pixelSize: 16
                    font.family: root.theme.fontFamily
                    visible: resultGrid.count === 0
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: root.model && root.model.loading
                    visible: running
                    width: 48
                    height: 48
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 30 : 0
                visible: root.model && searchInput.text.trim().length === 0
                    && (root.model.currentPage > 1 || root.model.hasNextPage)
                spacing: 8

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 28
                    radius: 5
                    color: previousPageMouse.enabled
                        ? (previousPageMouse.containsMouse
                            ? root.theme.panelRaisedColor : root.theme.panelColor)
                        : root.theme.surfaceColor
                    border.color: previousPageMouse.enabled
                        ? root.theme.borderColor : root.theme.subtleBorderColor
                    opacity: previousPageMouse.enabled ? 1.0 : 0.55

                    Text {
                        anchors.centerIn: parent
                        text: "上一页"
                        color: root.theme.textSecondaryColor
                        font.pixelSize: 11
                        font.family: root.theme.fontFamily
                    }

                    MouseArea {
                        id: previousPageMouse
                        anchors.fill: parent
                        enabled: root.model && !root.model.loading
                            && root.model.currentPage > 1
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.model.loadRecommended(root.model.currentPage - 1)
                    }
                }

                Text {
                    text: root.model ? ("第 " + root.model.currentPage + " 页") : ""
                    color: root.theme.textMutedColor
                    font.pixelSize: 11
                    font.family: root.theme.fontFamily
                }

                Rectangle {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 28
                    radius: 5
                    color: nextPageMouse.enabled
                        ? (nextPageMouse.containsMouse
                            ? root.theme.panelRaisedColor : root.theme.panelColor)
                        : root.theme.surfaceColor
                    border.color: nextPageMouse.enabled
                        ? root.theme.borderColor : root.theme.subtleBorderColor
                    opacity: nextPageMouse.enabled ? 1.0 : 0.55

                    Text {
                        anchors.centerIn: parent
                        text: "下一页"
                        color: root.theme.textSecondaryColor
                        font.pixelSize: 11
                        font.family: root.theme.fontFamily
                    }

                    MouseArea {
                        id: nextPageMouse
                        anchors.fill: parent
                        enabled: root.model && !root.model.loading
                            && root.model.hasNextPage
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.model.loadRecommended(root.model.currentPage + 1)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // ── 追剧日历 (放在近期热播下方) ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 14
                Layout.preferredHeight: 248
                Layout.maximumHeight: 248
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "追剧日历"
                        color: root.theme.textPrimaryColor
                        font.pixelSize: 14
                        font.bold: true
                        font.family: root.theme.fontFamily
                    }
                    Text {
                        text: "近期更新时间表"
                        color: root.theme.textMutedColor
                        font.pixelSize: 11
                        font.family: root.theme.fontFamily
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.schedule && root.schedule.loading ? "加载中..." : ""
                        color: root.theme.textMutedColor
                        font.pixelSize: 11
                        font.family: root.theme.fontFamily
                        visible: text.length > 0
                    }
                    Text {
                        text: root.schedule && root.schedule.errorMessage ? root.schedule.errorMessage : ""
                        color: root.theme.dangerColor
                        font.pixelSize: 11
                        font.family: root.theme.fontFamily
                        visible: text.length > 0
                    }
                    Rectangle {
                        Layout.preferredWidth: 54
                        Layout.preferredHeight: 24
                        radius: 5
                        color: scheduleRefreshMouse.containsMouse
                            ? root.theme.panelRaisedColor : root.theme.panelColor
                        border.color: root.theme.borderColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "刷新"
                            color: root.theme.textSecondaryColor
                            font.pixelSize: 11
                            font.family: root.theme.fontFamily
                        }

                        MouseArea {
                            id: scheduleRefreshMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: if (root.schedule) root.schedule.load()
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Flickable {
                        anchors.fill: parent
                        contentWidth: scheduleRow.implicitWidth
                        contentHeight: scheduleRow.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick

                        Row {
                        id: scheduleRow
                        spacing: 10

                        Repeater {
                            model: root.schedule ? root.schedule.days : []

                            Rectangle {
                                width: 210
                                height: scheduleRow.implicitHeight > 0
                                    ? Math.max(190, dayColumn.implicitHeight + 16) : 190
                                radius: root.theme.controlRadius
                                color: root.theme.panelColor
                                border.color: root.theme.borderColor
                                border.width: 1

                                ColumnLayout {
                                    id: dayColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 6

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.date || ""
                                        color: root.theme.textPrimaryColor
                                        font.pixelSize: 13
                                        font.bold: true
                                        font.family: root.theme.fontFamily
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: {
                                            var names = ["日","一","二","三","四","五","六"]
                                            return "周" + (names[modelData.weekday] || "?")
                                        }
                                        color: root.theme.textMutedColor
                                        font.pixelSize: 11
                                        font.family: root.theme.fontFamily
                                    }

                                    Repeater {
                                        model: modelData.items || []

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 58
                                            radius: 6
                                            color: root.theme.surfaceColor
                                            border.color: root.theme.subtleBorderColor
                                            border.width: 1

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 8

                                                Rectangle {
                                                    Layout.preferredWidth: 40
                                                    Layout.preferredHeight: 46
                                                    radius: 4
                                                    color: root.theme.surfaceColor
                                                    clip: true

                                                    Image {
                                                        anchors.fill: parent
                                                        source: modelData.cover || ""
                                                        fillMode: Image.PreserveAspectCrop
                                                        asynchronous: true
                                                        cache: true
                                                        visible: modelData.cover
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 1

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: modelData.title || "未命名"
                                                        color: root.theme.textPrimaryColor
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                        font.family: root.theme.fontFamily
                                                        elide: Text.ElideRight
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: [modelData.episodeStatus, modelData.deltaEpisode]
                                                            .filter(function(s){ return s && s.length>0 }).join(" · ")
                                                        color: root.theme.textMutedColor
                                                        font.pixelSize: 10
                                                        font.family: root.theme.fontFamily
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: {
                            if (!root.schedule)
                                return ""
                            if (root.schedule.loading)
                                return "追剧日历加载中..."
                            if (root.schedule.errorMessage && root.schedule.errorMessage.length > 0)
                                return root.schedule.errorMessage
                            if (root.schedule.days.length === 0)
                                return "近期暂无追剧日历数据"
                            return ""
                        }
                        color: root.schedule && root.schedule.errorMessage
                            && root.schedule.errorMessage.length > 0
                            ? root.theme.dangerColor : root.theme.textMutedColor
                        font.pixelSize: 13
                        font.family: root.theme.fontFamily
                        visible: text.length > 0
                    }
                }
            }
            }
        }

        // ── Page 1: 详情 ──
        Item {
            VideoDetailPanel {
                anchors.fill: parent
                theme: root.theme
                sidebar: root.sidebar
                vodId: Number(root.detailValue("vodId") || 0)
                vodName: String(root.detailValue("vodName") || "")
                vodPic: String(root.detailValue("vodPic") || "")
                vodRemarks: String(root.detailValue("vodRemarks") || "")
                vodYear: String(root.detailValue("vodYear") || "")
                vodArea: String(root.detailValue("vodArea") || "")
                vodClass: String(root.detailValue("vodClass") || root.detailValue("typeName") || "")
                vodActor: String(root.detailValue("vodActor") || "")
                vodDirector: String(root.detailValue("vodDirector") || "")
                vodBlurb: String(root.detailValue("vodBlurb") || "")
                vodContent: String(root.detailValue("vodContent") || "")
                vodPlayFrom: String(root.detailValue("vodPlayFrom") || "")
                vodPlayUrl: String(root.detailValue("vodPlayUrl") || "")
                vodScore: String(root.detailValue("vodScore") || "")
                onBackRequested: root.detailActive = false
                onPlaybackRequested: root.playbackRequested()
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.model && root.model.detailLoading
                visible: running
                width: 48
                height: 48
            }

            Text {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 4
                text: root.model && root.model.errorMessage ? root.model.errorMessage : ""
                color: root.theme.dangerColor
                font.pixelSize: root.theme.captionSize
                font.family: root.theme.fontFamily
                visible: text.length > 0
            }
        }
    }

    Component.onCompleted: {
        root.ensureRecommended()
        root.ensureScheduleLoaded()
    }
    onVisibleChanged: if (visible) {
        root.ensureRecommended()
        root.ensureScheduleLoaded()
    }
}
