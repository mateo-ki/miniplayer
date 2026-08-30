pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 阅读（Legado 书源）：导入书源 → 搜索/发现 → 详情 → 目录 → 正文/听书。
Rectangle {
    id: root
    color: "transparent"

    property var controller
    property var theme
    property var sidebar
    readonly property var sourceModel: controller ? services.legadoSourceModel : null
    readonly property var model: controller ? services.legadoBookModel : null
    property bool importOpen: false
    property bool tocOpen: false
    property bool readerOpen: false

    signal playbackRequested()

    function detailValue(name) {
        if (!root.model || !root.model.detail)
            return ""
        var value = root.model.detail[name]
        return value === undefined || value === null ? "" : String(value)
    }

    function doSearch() {
        if (!root.model || root.model.loading)
            return
        root.detailActive = false
        root.model.search(searchInput.text)
    }

    property bool detailActive: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.theme.edgePadding
        spacing: 8

        // ── 顶部工具栏 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            radius: root.theme.controlRadius
            color: root.theme.panelColor
            border.color: root.theme.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Text {
                    text: "📖"
                    font.pixelSize: 18
                }
                Text {
                    text: "阅读"
                    color: root.theme.textPrimaryColor
                    font.pixelSize: 15
                    font.bold: true
                    font.family: root.theme.fontFamily
                }

                ComboBox {
                    id: sourceCombo
                    Layout.preferredWidth: 210
                    model: root.sourceModel
                    textRole: "name"
                    currentIndex: root.sourceModel ? root.sourceModel.selectedIndex : -1
                    displayText: currentIndex >= 0 ? currentText : "选择书源"
                    font.family: root.theme.fontFamily
                    onActivated: function(index) {
                        if (root.sourceModel) {
                            root.sourceModel.selectSource(index)
                            root.detailActive = false
                        }
                    }
                }

                Rectangle {
                    Layout.preferredHeight: 32
                    Layout.preferredWidth: 54
                    radius: root.theme.controlRadius
                    color: importButtonMouse.containsMouse
                        ? root.theme.panelRaisedColor : "transparent"
                    border.color: root.theme.subtleBorderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "导入"
                        color: root.importOpen ? root.theme.accentColor : root.theme.textSecondaryColor
                        font.pixelSize: 12
                        font.family: root.theme.fontFamily
                    }
                    MouseArea {
                        id: importButtonMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.importOpen = !root.importOpen
                    }
                }

                Item { Layout.fillWidth: true }

                TextField {
                    id: searchInput
                    Layout.preferredWidth: Math.max(160, root.width * 0.26)
                    placeholderText: "搜索书籍…"
                    font.family: root.theme.fontFamily
                    enabled: root.model && root.model.hasSource && !root.model.loading
                    onAccepted: root.doSearch()
                }
                Button {
                    text: root.model && root.model.loading ? "…" : "搜索"
                    font.family: root.theme.fontFamily
                    enabled: root.model && root.model.hasSource && !root.model.loading
                    onClicked: root.doSearch()
                }

                ComboBox {
                    id: exploreCombo
                    Layout.preferredWidth: 170
                    visible: root.model && root.model.hasExplore
                    model: root.model ? root.model.exploreEntries : []
                    textRole: "name"
                    displayText: "发现"
                    font.family: root.theme.fontFamily
                    onActivated: function(index) {
                        if (root.model) {
                            root.detailActive = false
                            root.model.loadExploreEntry(index)
                        }
                    }
                }
            }
        }

        // ── 导入面板 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: importOpen ? 190 : 0
            visible: importOpen
            clip: true
            radius: root.theme.controlRadius
            color: root.theme.panelColor
            border.color: root.theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                visible: importOpen

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: importUrlInput
                        Layout.fillWidth: true
                        placeholderText: "书源链接，例如 https://www.yckceo.com/yuedu/shuyuans/json/id/1248.json"
                        font.family: root.theme.fontFamily
                        selectByMouse: true
                    }
                    Button {
                        text: "导入链接"
                        font.family: root.theme.fontFamily
                        enabled: root.sourceModel && !root.sourceModel.importing
                        onClicked: {
                            if (root.sourceModel) {
                                root.sourceModel.importFromUrl(importUrlInput.text)
                                importUrlInput.text = ""
                            }
                        }
                    }
                }

                TextArea {
                    id: importTextInput
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    placeholderText: "或在此粘贴书源 JSON 文本（支持单个对象或数组）"
                    wrapMode: TextArea.WrapAnywhere
                    font.family: root.theme.fontFamily
                    font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        text: "导入粘贴内容"
                        font.family: root.theme.fontFamily
                        enabled: root.sourceModel && !root.sourceModel.importing
                            && importTextInput.text.trim().length > 0
                        onClicked: {
                            if (root.sourceModel) {
                                root.sourceModel.importFromText(importTextInput.text)
                                importTextInput.text = ""
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "清空全部书源"
                        font.family: root.theme.fontFamily
                        enabled: root.sourceModel && root.sourceModel.count > 0
                        onClicked: {
                            if (root.sourceModel)
                                root.sourceModel.clearAll()
                        }
                    }
                }
            }
        }

        // ── 列表 / 详情 ──
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.detailActive ? 1 : 0

            // 列表页
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: booksGrid
                    anchors.fill: parent
                    clip: true
                    model: root.model
                    cellWidth: Math.floor(width / Math.max(2, Math.floor(width / 168)))
                    cellHeight: 232
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: card
                        required property string name
                        required property string author
                        required property string cover
                        required property string kind
                        required property int index

                        width: booksGrid.cellWidth
                        height: booksGrid.cellHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 5
                            radius: 10
                            color: cardMouse.containsMouse
                                ? root.theme.panelRaisedColor : root.theme.panelColor
                            border.color: root.theme.subtleBorderColor
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 6
                                    color: root.theme.surfaceColor
                                    clip: true

                                    Text {
                                        anchors.centerIn: parent
                                        visible: card.cover.length === 0
                                        text: "📚"
                                        font.pixelSize: 34
                                        opacity: 0.5
                                    }
                                    Image {
                                        anchors.fill: parent
                                        visible: card.cover.length > 0
                                        source: card.cover
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: card.name
                                    color: root.theme.textPrimaryColor
                                    font.pixelSize: 12
                                    font.family: root.theme.fontFamily
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (card.author || "") + (card.kind ? " · " + card.kind : "")
                                    color: root.theme.textMutedColor
                                    font.pixelSize: 10
                                    font.family: root.theme.fontFamily
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }

                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.detailActive = true
                                    if (root.model)
                                        root.model.loadDetail(card.index)
                                }
                            }
                        }
                    }
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: root.model && root.model.loading
                    visible: running
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.model && !root.model.loading && root.model.count === 0
                    text: root.model && root.model.hasSource
                        ? "搜索或从「发现」加载书籍"
                        : "请先在左上角导入并选择书源"
                    color: root.theme.textMutedColor
                    font.pixelSize: 13
                    font.family: root.theme.fontFamily
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    visible: root.model && root.model.hasNextPage && root.model.count > 0
                    text: "加载更多"
                    font.family: root.theme.fontFamily
                    enabled: root.model && !root.model.loading
                    onClicked: {
                        if (root.model)
                            root.model.loadMore()
                    }
                }
            }

            // 详情页
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Flickable {
                    id: detailFlick
                    anchors.fill: parent
                    contentWidth: width
                    contentHeight: detailColumn.implicitHeight + 20
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: detailColumn
                        x: 0
                        y: 8
                        width: detailFlick.width - 16
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 14

                            Rectangle {
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 168
                                radius: 8
                                color: root.theme.surfaceColor
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: root.detailValue("cover")
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    visible: root.detailValue("cover").length > 0
                                        && status !== Image.Error
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: root.detailValue("cover").length === 0
                                    text: "📚"
                                    font.pixelSize: 40
                                    opacity: 0.5
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    Layout.fillWidth: true
                                    text: root.detailValue("name")
                                    color: root.theme.textPrimaryColor
                                    font.pixelSize: 18
                                    font.bold: true
                                    font.family: root.theme.fontFamily
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.detailValue("author")
                                    color: root.theme.textSecondaryColor
                                    font.pixelSize: 13
                                    font.family: root.theme.fontFamily
                                    elide: Text.ElideRight
                                    visible: text.length > 0
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.detailValue("kind")
                                    color: root.theme.textMutedColor
                                    font.pixelSize: 12
                                    font.family: root.theme.fontFamily
                                    elide: Text.ElideRight
                                    visible: text.length > 0
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "最新: " + root.detailValue("lastChapter")
                                    color: root.theme.textMutedColor
                                    font.pixelSize: 12
                                    font.family: root.theme.fontFamily
                                    elide: Text.ElideRight
                                    visible: root.detailValue("lastChapter").length > 0
                                }

                                RowLayout {
                                    spacing: 8
                                    Button {
                                        text: root.model && root.model.isAudioSource
                                            ? "🎧 选集播放" : "📑 目录"
                                        font.family: root.theme.fontFamily
                                        enabled: root.model && root.model.hasDetail
                                            && !root.model.tocLoading
                                        onClicked: {
                                            if (!root.model)
                                                return
                                            root.model.loadToc()
                                            root.tocOpen = true
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: root.theme.subtleBorderColor
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.detailValue("intro") || "（无简介）"
                            color: root.theme.textSecondaryColor
                            font.pixelSize: 13
                            font.family: root.theme.fontFamily
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                    }
                }

                Button {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    text: "← 返回"
                    font.family: root.theme.fontFamily
                    onClicked: root.detailActive = false
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: root.model && root.model.detailLoading
                    visible: running
                }
            }
        }

        // ── 状态栏 ──
        Text {
            Layout.fillWidth: true
            text: root.model ? root.model.message : ""
            color: root.theme.textMutedColor
            font.pixelSize: 11
            font.family: root.theme.fontFamily
            elide: Text.ElideRight
        }
    }

    // ── 目录抽屉 ──
    Rectangle {
        id: tocDrawer
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(340, root.width * 0.75)
        color: root.theme.panelColor
        border.color: root.theme.borderColor
        border.width: 1
        z: 5
        visible: x < root.width
        x: root.tocOpen ? root.width - width : root.width + 4
        Behavior on x { NumberAnimation { duration: 180 } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: "目录 " + (root.model ? "(" + root.model.chapterCount + ")" : "")
                    color: root.theme.textPrimaryColor
                    font.pixelSize: 14
                    font.bold: true
                    font.family: root.theme.fontFamily
                    elide: Text.ElideRight
                }
                Button {
                    text: "✕"
                    font.family: root.theme.fontFamily
                    onClicked: root.tocOpen = false
                }
            }

            ListView {
                id: chapterList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.model ? root.model.chapters : []
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: chapterList.width
                    height: 34

                    contentItem: Text {
                        text: modelData.isVolume ? "▎" + modelData.title : modelData.title
                        color: modelData.isVolume
                            ? root.theme.accentColor : root.theme.textSecondaryColor
                        font.pixelSize: modelData.isVolume ? 13 : 12
                        font.bold: modelData.isVolume
                        font.family: root.theme.fontFamily
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: chapterItemMouse.containsMouse
                            ? root.theme.panelRaisedColor : "transparent"
                        radius: 6
                    }

                    MouseArea {
                        id: chapterItemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: modelData.isVolume ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: {
                            if (!root.model || modelData.isVolume)
                                return
                            if (root.model.isAudioSource) {
                                root.model.playChapter(index)
                                root.tocOpen = false
                                root.playbackRequested()
                            } else {
                                root.model.loadContent(index)
                                root.tocOpen = false
                                root.readerOpen = true
                            }
                        }
                    }
                }
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.model && root.model.tocLoading
                visible: running
                width: 36
                height: 36
            }
        }
    }

    // 遮挡列表点击
    MouseArea {
        anchors.fill: parent
        visible: root.tocOpen
        enabled: root.tocOpen
        z: 4
        onClicked: root.tocOpen = false
    }

    // ── 阅读器 ──
    Rectangle {
        id: reader
        anchors.fill: parent
        visible: root.readerOpen
        color: "#1c1a17"
        z: 10

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "✕ 关闭"
                    font.family: root.theme.fontFamily
                    onClicked: root.readerOpen = false
                }
                Text {
                    Layout.fillWidth: true
                    text: root.model ? root.model.contentChapterTitle : ""
                    color: "#d8d2c8"
                    font.pixelSize: 13
                    font.bold: true
                    font.family: root.theme.fontFamily
                    elide: Text.ElideRight
                }
                Button {
                    text: "上一章"
                    font.family: root.theme.fontFamily
                    enabled: root.model && root.model.contentChapterIndex > 0
                        && !root.model.contentLoading
                    onClicked: {
                        if (root.model)
                            root.model.loadContent(root.model.contentChapterIndex - 1)
                    }
                }
                Button {
                    text: "下一章"
                    font.family: root.theme.fontFamily
                    enabled: root.model && root.model.contentChapterIndex >= 0
                        && root.model.contentChapterIndex < root.model.chapterCount - 1
                        && !root.model.contentLoading
                    onClicked: {
                        if (root.model)
                            root.model.loadContent(root.model.contentChapterIndex + 1)
                    }
                }
            }

            // 图片源（漫画）：正文每行一个图片地址
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.model && root.model.isImageSource
                contentWidth: width
                contentHeight: imageColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: imageColumn
                    width: parent ? parent.width : 0
                    spacing: 4

                    Repeater {
                        model: root.model
                            ? root.model.content.split("\n").filter(function (l) {
                                  return l.trim().length > 0
                              })
                            : []
                        Image {
                            required property string modelData
                            Layout.fillWidth: true
                            source: modelData
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }
            }

            // 文本阅读
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.model && !root.model.isImageSource
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    id: readerText
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    text: root.model ? root.model.content : ""
                    color: "#d8d2c8"
                    font.pixelSize: 17
                    font.family: root.theme.fontFamily
                    textFormat: Text.PlainText
                    background: Rectangle { color: "transparent" }
                }
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.model && root.model.contentLoading
                visible: running
            }
        }
    }
}
