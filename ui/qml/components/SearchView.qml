import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"
    property var theme
    property var sidebar: null
    property int selectedVodId: 0
    property string selectedTypeId: ""
    property string selectedParentTypeId: ""
    property var categoryTags: []
    property var childCategoryTags: []
    property var activeCategoryPopup: null
    property bool detailActive: false
    property bool siteDropdownOpen: false
    signal manageSitesRequested()
    signal playbackRequested()

    ListModel {
        id: videoSiteList
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: detailActive ? 1 : 0

        // Page 0: search results
        ColumnLayout {
            anchors.fill: parent
            spacing: 12

        // Top bar: site selector + search
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

                // API site selector
                Rectangle {
                    id: siteSelector
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 32
                    radius: 6
                    color: siteMouse.containsMouse || root.siteDropdownOpen ? theme.panelRaisedColor : theme.surfaceColor
                    border.color: root.siteDropdownOpen ? theme.accentColor : theme.borderColor
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: root.selectedVideoSiteName()
                            color: theme.textPrimaryColor
                            font.pixelSize: 12
                            font.family: theme.fontFamily
                            elide: Text.ElideRight
                        }

                        Text {
                            text: root.siteDropdownOpen ? "^" : "v"
                            color: root.siteDropdownOpen ? theme.accentColor : theme.textMutedColor
                            font.pixelSize: 10
                        }
                    }

                    MouseArea {
                        id: siteMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.rebuildVideoSites()
                            root.ensureVideoSiteSelected()
                            if (sitePopup.opened) {
                                sitePopup.close()
                            } else {
                                sitePopup.open()
                            }
                        }
                    }

                    Popup {
                        id: sitePopup
                        x: 0
                        y: siteSelector.height + 8
                        width: 320
                        height: Math.min(360, Math.max(1, videoSiteList.count) * 46 + 62)
                        padding: 8
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        onOpened: root.siteDropdownOpen = true
                        onClosed: root.siteDropdownOpen = false

                        background: Rectangle {
                            radius: 12
                            color: theme.panelColor
                            border.color: theme.borderColor
                            border.width: 1
                        }

                        contentItem: ColumnLayout {
                            spacing: 6

                            ListView {
                                id: videoSiteView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: videoSiteList
                                spacing: 4
                                boundsBehavior: Flickable.StopAtBounds

                                ScrollBar.vertical: ScrollBar {
                                    policy: videoSiteView.contentHeight > videoSiteView.height
                                        ? ScrollBar.AsNeeded
                                        : ScrollBar.AlwaysOff
                                }

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 42
                                    radius: 8
                                    color: root.currentVideoSiteIndex() === model.sourceIndex
                                        ? theme.accentMutedColor
                                        : (siteRowMouse.containsMouse ? theme.panelRaisedColor : "transparent")
                                    border.color: root.currentVideoSiteIndex() === model.sourceIndex ? theme.accentColor : "transparent"
                                    border.width: 1

                                    MouseArea {
                                        id: siteRowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.closeCategoryPopup()
                                            playerController.apiSiteModel.selectAt(model.sourceIndex)
                                            sitePopup.close()
                                            root.detailActive = false
                                            root.selectedTypeId = ""
                                            root.selectedParentTypeId = ""
                                            root.childCategoryTags = []
                                            root.loadPage(1)
                                        }
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 8

                                        Rectangle {
                                            Layout.preferredWidth: 18
                                            Layout.preferredHeight: 18
                                            radius: 9
                                            color: root.currentVideoSiteIndex() === model.sourceIndex
                                                ? "#183b29"
                                                : "transparent"
                                            border.color: root.currentVideoSiteIndex() === model.sourceIndex
                                                ? "#3ddc84"
                                                : "transparent"
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: root.currentVideoSiteIndex() === model.sourceIndex ? "✓" : ""
                                                color: "#7ff0ad"
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1

                                            Text {
                                                Layout.fillWidth: true
                                                text: model.siteName
                                                color: theme.textPrimaryColor
                                                font.pixelSize: 12
                                                font.bold: root.currentVideoSiteIndex() === model.sourceIndex
                                                font.family: theme.fontFamily
                                                elide: Text.ElideRight
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: model.siteBaseUrl
                                                color: theme.textMutedColor
                                                font.pixelSize: 10
                                                font.family: theme.fontFamily
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Rectangle {
                                            visible: model.sitePremium
                                            Layout.preferredWidth: premiumSiteText.implicitWidth + 14
                                            Layout.preferredHeight: 20
                                            radius: 6
                                            color: "#3a311a"
                                            border.color: "#b99135"
                                            border.width: 1

                                            Text {
                                                id: premiumSiteText
                                                anchors.centerIn: parent
                                                text: "优质"
                                                color: "#f2d27a"
                                                font.pixelSize: 10
                                                font.bold: true
                                                font.family: theme.fontFamily
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: theme.subtleBorderColor
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                radius: 8
                                color: manageSiteMouse.containsMouse ? theme.panelRaisedColor : "transparent"
                                border.color: manageSiteMouse.containsMouse ? theme.borderColor : "transparent"
                                border.width: 1

                                MouseArea {
                                    id: manageSiteMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        sitePopup.close()
                                        root.manageSitesRequested()
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8

                                    Text {
                                        text: "+"
                                        color: theme.accentColor
                                        font.pixelSize: 18
                                        font.bold: true
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "管理站点..."
                                        color: theme.textSecondaryColor
                                        font.pixelSize: 12
                                        font.family: theme.fontFamily
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                // Search input
                TextField {
                    id: searchInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    placeholderText: "搜索视频..."
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

                // Search button
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
            }
        }

        Rectangle {
            Layout.fillWidth: false
            Layout.preferredWidth: 0
            Layout.maximumWidth: 0
            Layout.alignment: Qt.AlignLeft
            Layout.preferredHeight: 0
            visible: false
            radius: theme.controlRadius
            color: theme.panelColor
            border.color: theme.borderColor
            border.width: 1
            clip: true

            ListView {
                id: inlineVideoSiteView
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: videoSiteList
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 42
                    radius: 8
                    color: root.currentVideoSiteIndex() === model.sourceIndex
                        ? theme.accentMutedColor
                        : (inlineSiteRowMouse.containsMouse ? theme.panelRaisedColor : "transparent")
                    border.color: root.currentVideoSiteIndex() === model.sourceIndex ? theme.accentColor : "transparent"
                    border.width: 1

                    MouseArea {
                        id: inlineSiteRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.closeCategoryPopup()
                            playerController.apiSiteModel.selectAt(model.sourceIndex)
                            root.siteDropdownOpen = false
                            root.detailActive = false
                            root.selectedTypeId = ""
                            root.selectedParentTypeId = ""
                            root.childCategoryTags = []
                            root.loadPage(1)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            radius: 9
                            color: root.currentVideoSiteIndex() === model.sourceIndex
                                ? "#183b29"
                                : "transparent"
                            border.color: root.currentVideoSiteIndex() === model.sourceIndex
                                ? "#3ddc84"
                                : "transparent"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: root.currentVideoSiteIndex() === model.sourceIndex ? "✓" : ""
                                color: "#7ff0ad"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: model.siteName
                                color: theme.textPrimaryColor
                                font.pixelSize: 12
                                font.bold: root.currentVideoSiteIndex() === model.sourceIndex
                                font.family: theme.fontFamily
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: model.siteBaseUrl
                                color: theme.textMutedColor
                                font.pixelSize: 10
                                font.family: theme.fontFamily
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            visible: root.categoryTags.length > 1
            clip: true
            contentWidth: categoryRow.implicitWidth
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: categoryRow
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter

                Repeater {
                    model: root.categoryTags

                    delegate: Rectangle {
                        id: categoryDelegate
                        width: Math.max(52, categoryText.implicitWidth + 24)
                        height: 26
                        radius: 13
                        color: root.selectedParentTypeId === (modelData.typeId || "")
                            ? theme.accentMutedColor
                            : (categoryMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor)
                        border.color: root.selectedParentTypeId === (modelData.typeId || "")
                            ? theme.accentColor
                            : theme.borderColor
                        border.width: 1

                        MouseArea {
                            id: categoryMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectParentCategory(modelData.typeId || "", categoryPopup)
                        }

                        Text {
                            id: categoryText
                            anchors.centerIn: parent
                            text: modelData.typeName || ""
                            color: root.selectedParentTypeId === (modelData.typeId || "")
                                ? theme.textPrimaryColor
                                : theme.textSecondaryColor
                            font.pixelSize: 12
                            font.family: theme.fontFamily
                            elide: Text.ElideRight
                        }

                        Popup {
                            id: categoryPopup
                            x: {
                                var pageX = categoryDelegate.mapToItem(root, 0, 0).x
                                return Math.max(-pageX + 8,
                                                Math.min(0, root.width - pageX - width - 8))
                            }
                            y: categoryDelegate.height + 4
                            width: Math.min(Math.max(140, childCategoryPopupRow.implicitWidth + 20),
                                            Math.max(140, root.width - 16))
                            height: 44
                            padding: 8
                            modal: false
                            focus: true
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                            onClosed: {
                                if (root.activeCategoryPopup === categoryPopup)
                                    root.activeCategoryPopup = null
                            }

                            background: Rectangle {
                                radius: 6
                                color: theme.panelRaisedColor
                                border.color: theme.borderColor
                                border.width: 1
                            }

                            contentItem: Flickable {
                                clip: true
                                contentWidth: childCategoryPopupRow.implicitWidth
                                contentHeight: height
                                boundsBehavior: Flickable.StopAtBounds

                                Row {
                                    id: childCategoryPopupRow
                                    spacing: 8
                                    anchors.verticalCenter: parent.verticalCenter

                                    Repeater {
                                        model: root.childCategoryTags

                                        delegate: Rectangle {
                                            width: Math.max(52, childCategoryText.implicitWidth + 24)
                                            height: 26
                                            radius: 13
                                            color: root.selectedTypeId === (modelData.typeId || "")
                                                ? theme.accentMutedColor
                                                : (childCategoryMouse.containsMouse
                                                   ? theme.surfaceColor : theme.panelColor)
                                            border.color: root.selectedTypeId === (modelData.typeId || "")
                                                ? theme.accentColor : theme.borderColor
                                            border.width: 1

                                            MouseArea {
                                                id: childCategoryMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.selectChildCategory(modelData.typeId || "")
                                            }

                                            Text {
                                                id: childCategoryText
                                                anchors.centerIn: parent
                                                text: modelData.typeName || ""
                                                color: root.selectedTypeId === (modelData.typeId || "")
                                                    ? theme.textPrimaryColor : theme.textSecondaryColor
                                                font.pixelSize: 12
                                                font.family: theme.fontFamily
                                                elide: Text.ElideRight
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

        // Status bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4

                Text {
                    text: playerController.videoSearchModel.loading ? "加载中..." :
                          (playerController.videoSearchModel.totalCount > 0 ? "共 " + playerController.videoSearchModel.totalCount + " 条结果" : (playerController.videoSearchModel.errorMessage ? "错误: " + playerController.videoSearchModel.errorMessage : ""))
                    color: playerController.videoSearchModel.errorMessage ? theme.dangerColor : theme.textMutedColor
                    font.pixelSize: 11
                    font.family: theme.fontFamily
                }

                Item { Layout.fillWidth: true }
            }
        }

        // Results grid
        GridView {
            id: resultGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 200
            cellHeight: 300
            clip: true
            model: playerController.videoSearchModel

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
                    z: 20
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                        playerController.resetMouseCursor()
                        var vod = playerController.videoSearchModel.itemMapAt(index)
                        if (!vod) return
                        root.selectedVodId = vod.vodId || 0
                        root.showVodDetail(vod)
                        if (root.selectedVodId > 0) {
                            playerController.loadVideoDetail(root.selectedVodId)
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    // Poster image
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

                            // Loading indicator
                            BusyIndicator {
                                anchors.centerIn: parent
                                running: parent.status === Image.Loading
                                visible: running
                                width: 32
                                height: 32
                            }

                            // Fallback
                            Text {
                                anchors.centerIn: parent
                                text: "馃幀"
                                font.pixelSize: 48
                                visible: parent.status === Image.Error || !model.vodPic
                                color: theme.textMutedColor
                            }
                        }

                    }

                    // Title
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

                    // Meta info
                    Text {
                        Layout.fillWidth: true
                        Layout.rightMargin: remarksBadge.visible ? remarksBadge.width + 4 : 0
                        text: {
                            var parts = []
                            if (model.vodYear) parts.push(model.vodYear)
                            if (model.vodArea) parts.push(model.vodArea)
                            if (model.vodClass) parts.push(model.vodClass)
                            if (model.vodScore && model.vodScore !== "0.0") parts.push("★ " + model.vodScore)
                            return parts.join(" · ")
                        }
                        color: theme.textMutedColor
                        font.pixelSize: 10
                        font.family: theme.fontFamily
                        elide: Text.ElideRight
                    }
                }

                // Episode/update text belongs to the card, not the poster image.
                Rectangle {
                    id: remarksBadge
                    visible: model.vodRemarks && model.vodRemarks.length > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 8
                    width: Math.min(implicitWidth, parent.width - 16)
                    height: 20
                    implicitWidth: remarksLabel.implicitWidth + 10
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

            // Empty state
            Text {
                anchors.centerIn: parent
                text: playerController.videoSearchModel.loading ? "" : "输入关键词搜索视频"
                color: theme.textMutedColor
                font.pixelSize: 16
                font.family: theme.fontFamily
                visible: resultGrid.count === 0
            }

            // Loading overlay
            BusyIndicator {
                anchors.centerIn: parent
                running: playerController.videoSearchModel.loading
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
            visible: playerController.videoSearchModel
                && (playerController.videoSearchModel.currentPage > 1
                    || playerController.videoSearchModel.currentPage
                        < playerController.videoSearchModel.totalPages)

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 72
                Layout.preferredHeight: 28
                radius: 5
                color: videoPreviousMouse.enabled
                    ? (videoPreviousMouse.containsMouse
                        ? theme.panelRaisedColor : theme.panelColor)
                    : theme.surfaceColor
                border.color: videoPreviousMouse.enabled
                    ? theme.borderColor : theme.subtleBorderColor
                opacity: videoPreviousMouse.enabled ? 1.0 : 0.55

                MouseArea {
                    id: videoPreviousMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !playerController.videoSearchModel.loading
                        && playerController.videoSearchModel.currentPage > 1
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.loadPage(playerController.videoSearchModel.currentPage - 1)
                }

                Text {
                    anchors.centerIn: parent
                    text: "上一页"
                    color: theme.textSecondaryColor
                    font.pixelSize: 11
                    font.family: theme.fontFamily
                }
            }

            Text {
                text: "第 " + playerController.videoSearchModel.currentPage + " 页"
                color: theme.textMutedColor
                font.pixelSize: 11
                font.family: theme.fontFamily
            }

            Rectangle {
                Layout.preferredWidth: 72
                Layout.preferredHeight: 28
                radius: 5
                color: videoNextMouse.enabled
                    ? (videoNextMouse.containsMouse
                        ? theme.panelRaisedColor : theme.panelColor)
                    : theme.surfaceColor
                border.color: videoNextMouse.enabled
                    ? theme.borderColor : theme.subtleBorderColor
                opacity: videoNextMouse.enabled ? 1.0 : 0.55

                MouseArea {
                    id: videoNextMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !playerController.videoSearchModel.loading
                        && playerController.videoSearchModel.currentPage
                            < playerController.videoSearchModel.totalPages
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.loadPage(playerController.videoSearchModel.currentPage + 1)
                }

                Text {
                    anchors.centerIn: parent
                    text: "下一页"
                    color: theme.textSecondaryColor
                    font.pixelSize: 11
                    font.family: theme.fontFamily
                }
            }

            Item { Layout.fillWidth: true }
        }
        }

        // Page 1: video detail (inline page, replaces the old popup overlay)
        VideoDetailPanel {
            id: videoDetailPanel
            theme: root.theme
            sidebar: root.sidebar
            onBackRequested: root.detailActive = false
            onPlaybackRequested: root.playbackRequested()
        }
    }

    Component.onCompleted: {
        refreshCategoryTags()
        root.rebuildVideoSites()
    }

    function doSearch() {
        root.siteDropdownOpen = false
        root.closeCategoryPopup()
        if (searchInput.text.trim().length > 0) {
            root.selectedTypeId = ""
            root.selectedParentTypeId = ""
            root.childCategoryTags = []
            playerController.searchVideos(searchInput.text.trim(), 1, true)
            return
        }
        loadPage(1)
    }

    function selectParentCategory(typeId, popup) {
        root.siteDropdownOpen = false
        root.selectedParentTypeId = typeId || ""
        root.refreshChildCategoryTags()
        searchInput.text = ""

        if (root.childCategoryTags.length > 0) {
            if (root.activeCategoryPopup && root.activeCategoryPopup !== popup)
                root.activeCategoryPopup.close()
            root.activeCategoryPopup = popup
            popup.open()
            return
        }

        root.closeCategoryPopup()
        root.selectedTypeId = root.selectedParentTypeId
        loadPage(1)
    }

    function selectChildCategory(typeId) {
        root.siteDropdownOpen = false
        root.selectedTypeId = typeId || ""
        searchInput.text = ""
        root.closeCategoryPopup()
        loadPage(1)
    }

    function closeCategoryPopup() {
        if (root.activeCategoryPopup) root.activeCategoryPopup.close()
        root.activeCategoryPopup = null
    }

    function loadPage(page) {
        var keyword = searchInput.text.trim()
        if (keyword.length === 0) {
            if (root.selectedTypeId.length > 0) {
                playerController.loadVideoListByCategory(root.selectedTypeId, page)
                return
            }
            playerController.loadVideoList(page)
            return
        }
        playerController.searchVideos(keyword, page)
    }

    function refreshCategoryTags() {
        var tags = [{ "typeId": "", "typeName": "全部" }]
        var categories = playerController.videoSearchModel.categories || []
        var hasHierarchy = false
        var selectedTypeExists = root.selectedTypeId.length === 0
        for (var hierarchyIndex = 0; hierarchyIndex < categories.length; ++hierarchyIndex) {
            var hierarchyItem = categories[hierarchyIndex]
            if (hierarchyItem && String(hierarchyItem.typeId || "") === root.selectedTypeId)
                selectedTypeExists = true
            if (hierarchyItem && hierarchyItem.parentTypeId
                    && String(hierarchyItem.parentTypeId) !== "0") {
                hasHierarchy = true
                break
            }
        }

        var selectedParentExists = root.selectedParentTypeId.length === 0
        for (var i = 0; i < categories.length; ++i) {
            var item = categories[i]
            if (!item || !item.typeId || !item.typeName) continue
            var parentTypeId = item.parentTypeId === undefined || item.parentTypeId === null
                ? "" : String(item.parentTypeId)
            if (hasHierarchy && parentTypeId.length > 0 && parentTypeId !== "0") continue
            tags.push({
                "typeId": String(item.typeId),
                "typeName": String(item.typeName),
                "parentTypeId": parentTypeId
            })
            if (root.selectedParentTypeId === String(item.typeId)) selectedParentExists = true
        }
        if (!selectedParentExists) {
            root.selectedParentTypeId = ""
        }
        if (!selectedTypeExists) root.selectedTypeId = ""
        root.categoryTags = tags
        root.refreshChildCategoryTags()
    }

    function refreshChildCategoryTags() {
        var children = []
        var categories = playerController.videoSearchModel.categories || []
        if (root.selectedParentTypeId.length > 0) {
            for (var i = 0; i < categories.length; ++i) {
                var item = categories[i]
                if (!item || !item.typeId || !item.typeName) continue
                var parentTypeId = item.parentTypeId === undefined || item.parentTypeId === null
                    ? "" : String(item.parentTypeId)
                if (parentTypeId !== root.selectedParentTypeId) continue
                children.push({
                    "typeId": String(item.typeId),
                    "typeName": String(item.typeName),
                    "parentTypeId": parentTypeId
                })
            }
        }
        root.childCategoryTags = children
    }

    function rebuildVideoSites() {
        videoSiteList.clear()
        for (var i = 0; i < playerController.apiSiteModel.count; ++i) {
            if (playerController.apiSiteModel.typeAt(i) !== "video") continue
            videoSiteList.append({
                "sourceIndex": i,
                "siteName": playerController.apiSiteModel.nameAt(i),
                "siteBaseUrl": playerController.apiSiteModel.baseUrlAt(i),
                "sitePremium": playerController.apiSiteModel.premiumAt(i)
            })
        }
    }

    function firstVideoSiteIndex() {
        for (var i = 0; i < playerController.apiSiteModel.count; ++i) {
            if (playerController.apiSiteModel.typeAt(i) === "video") return i
        }
        return -1
    }

    function ensureVideoSiteSelected() {
        var current = playerController.apiSiteModel.currentIndex
        if (current >= 0 && playerController.apiSiteModel.typeAt(current) === "video") return current
        var first = root.firstVideoSiteIndex()
        if (first >= 0) playerController.apiSiteModel.selectAt(first)
        return first
    }

    function currentVideoSiteIndex() {
        var current = playerController.apiSiteModel.currentIndex
        if (current >= 0 && playerController.apiSiteModel.typeAt(current) === "video") {
            return current
        }
        return root.firstVideoSiteIndex()
    }
    function selectedVideoSiteName() {
        var current = root.currentVideoSiteIndex()
        if (current >= 0) {
            return playerController.apiSiteModel.nameAt(current)
        }
        return "暂无视频站点"
    }

    function openListPage() {
        root.closeCategoryPopup()
        root.rebuildVideoSites()
        root.ensureVideoSiteSelected()
        root.detailActive = false
        root.selectedVodId = 0
        root.selectedTypeId = ""
        root.selectedParentTypeId = ""
        root.childCategoryTags = []
        searchInput.text = ""
        playerController.resetMouseCursor()
        loadPage(1)
    }

    function showVodDetail(vod) {
        playerController.setCurrentVodName(vod.vodName || "")
        videoDetailPanel.vodId = vod.vodId || 0
        videoDetailPanel.vodName = vod.vodName || ""
        videoDetailPanel.vodPic = vod.vodPic || ""
        videoDetailPanel.vodRemarks = vod.vodRemarks || ""
        videoDetailPanel.vodYear = vod.vodYear || ""
        videoDetailPanel.vodArea = vod.vodArea || ""
        videoDetailPanel.vodClass = vod.vodClass || ""
        videoDetailPanel.vodActor = vod.vodActor || ""
        videoDetailPanel.vodDirector = vod.vodDirector || ""
        videoDetailPanel.vodBlurb = vod.vodBlurb || ""
        videoDetailPanel.vodContent = vod.vodContent || ""
        videoDetailPanel.vodPlayFrom = vod.vodPlayFrom || ""
        videoDetailPanel.vodPlayUrl = vod.vodPlayUrl || ""
        videoDetailPanel.vodScore = vod.vodScore || ""
        root.detailActive = true
        playerController.resetMouseCursor()
    }


    Connections {
        target: playerController.apiSiteModel
        function onCurrentIndexChanged() {
            root.closeCategoryPopup()
            root.rebuildVideoSites()
            root.selectedTypeId = ""
            root.selectedParentTypeId = ""
            root.childCategoryTags = []
        }
        function onCountChanged() {
            root.rebuildVideoSites()
        }
        function onCurrentSiteChanged() {
            root.rebuildVideoSites()
        }
        function onDataChanged() {
            root.rebuildVideoSites()
        }
    }

    Connections {
        target: playerController.videoSearchModel
        function onCategoriesChanged() {
            root.refreshCategoryTags()
        }
    }

    Connections {
        target: playerController.detailSearchModel
        function onDetailReceived(index) {
            var vod = playerController.detailSearchModel.itemMapAt(index)
            if (!vod || !root.detailActive) return
            if (root.selectedVodId > 0 && vod.vodId !== root.selectedVodId) return
            root.showVodDetail(vod)
        }
    }
}

