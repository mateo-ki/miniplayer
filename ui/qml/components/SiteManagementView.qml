import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"
    property var controller
    property var theme
    property int editIndex: -1
    property bool isEditing: editIndex >= 0
    property string message: ""
    property bool sortingEnabled: siteSearchInput.text.trim().length === 0
                                  && root.selectedTypeFilterValue() === "all"
    property int reorderSourceIndex: -1
    property real reorderPointerY: 0
    property real reorderGrabOffsetY: 0

    function beginSiteReorder(sourceIndex, pointerY, rowY) {
        if (!root.sortingEnabled) return
        root.reorderSourceIndex = sourceIndex
        root.reorderPointerY = pointerY
        root.reorderGrabOffsetY = siteList.contentY + pointerY - rowY
        siteList.interactive = false
        reorderScrollTimer.start()
    }

    function updateSiteReorder(pointerY) {
        if (root.reorderSourceIndex < 0) return
        root.reorderPointerY = pointerY
    }

    function reorderVisualOffset(rowY) {
        if (root.reorderSourceIndex < 0) return 0
        return root.reorderPointerY - root.reorderGrabOffsetY - rowY + siteList.contentY
    }

    function finishSiteReorder(cancelled) {
        if (root.reorderSourceIndex < 0) return
        reorderScrollTimer.stop()
        siteList.interactive = true

        var sourceIndex = root.reorderSourceIndex
        root.reorderSourceIndex = -1
        if (!cancelled) {
            var step = 82 + siteList.spacing
            var pointerContentY = siteList.contentY + root.reorderPointerY
            var insertSlot = 0
            if (pointerContentY >= siteList.contentHeight) {
                insertSlot = filteredSiteList.count
            } else if (pointerContentY > 0) {
                var hoveredIndex = Math.min(filteredSiteList.count - 1,
                    Math.floor(pointerContentY / step))
                var hoveredTop = hoveredIndex * step
                insertSlot = hoveredIndex
                if (pointerContentY - hoveredTop >= 41)
                    insertSlot += 1
            }
            if (root.controller.apiSiteModel.moveSiteToSlot(sourceIndex, insertSlot)) {
                root.message = "已调整站点顺序"
            }
        }

        root.clampSiteListContentY()
    }

    Timer {
        id: reorderScrollTimer
        interval: 16
        repeat: true
        onTriggered: {
            if (root.reorderSourceIndex < 0 || siteList.contentHeight <= siteList.height) return
            var edgeSize = Math.min(72, siteList.height * 0.2)
            var delta = 0
            if (root.reorderPointerY < edgeSize) {
                delta = -Math.max(3, (edgeSize - root.reorderPointerY) * 0.22)
            } else if (root.reorderPointerY > siteList.height - edgeSize) {
                delta = Math.max(3, (root.reorderPointerY - siteList.height + edgeSize) * 0.22)
            }
            if (delta === 0) return
            var maximumY = Math.max(0, siteList.contentHeight - siteList.height)
            siteList.contentY = Math.max(0, Math.min(maximumY, siteList.contentY + delta))
        }
    }

    ListModel {
        id: filteredSiteList
    }

    function resetForm() {
        editIndex = -1
        nameInput.text = ""
        urlInput.text = ""
        typeInput.currentIndex = 0
    }

    function statusColor(status) {
        if (status === 1) return "#f5a623"
        if (status === 2) return "#3ddc84"
        if (status === 3) return theme.dangerColor
        return theme.textMutedColor
    }

    function siteTypeLabel(siteType) {
        if (siteType === "image") return "图片"
        if (siteType === "shortvideo") return "短视频"
        return "视频"
    }

    function siteTypeFillColor(siteType) {
        if (siteType === "image") return "#2b4a35"
        if (siteType === "shortvideo") return "#3b2b4a"
        return theme.surfaceColor
    }

    function siteTypeBorderColor(siteType) {
        if (siteType === "image") return "#3ddc84"
        if (siteType === "shortvideo") return "#b06cff"
        return theme.borderColor
    }

    function siteTypeTextColor(siteType) {
        if (siteType === "image") return "#3ddc84"
        if (siteType === "shortvideo") return "#d0a2ff"
        return theme.textSecondaryColor
    }

    function siteTypeInputIndex(siteType) {
        if (siteType === "image") return 1
        if (siteType === "shortvideo") return 2
        return 0
    }

    function siteTypeFromInputIndex(index) {
        if (index === 1) return "image"
        if (index === 2) return "shortvideo"
        return "video"
    }

    function selectedTypeFilterValue() {
        if (!typeFilter) return "all"
        if (typeFilter.currentIndex === 1) return "video"
        if (typeFilter.currentIndex === 2) return "shortvideo"
        if (typeFilter.currentIndex === 3) return "image"
        return "all"
    }

    function matchesTypeFilter(siteType) {
        var selectedType = root.selectedTypeFilterValue()
        return selectedType === "all" || siteType === selectedType
    }

    function refreshFilteredSites() {
        var previousContentY = siteList.contentY
        filteredSiteList.clear()
        for (var i = 0; i < root.controller.apiSiteModel.count; ++i) {
            var siteType = root.controller.apiSiteModel.typeAt(i)
            if (!root.controller.apiSiteModel.matchesFilter(i, siteSearchInput.text)) continue
            if (!root.matchesTypeFilter(siteType)) continue
            filteredSiteList.append({
                "sourceIndex": i,
                "name": root.controller.apiSiteModel.nameAt(i),
                "baseUrl": root.controller.apiSiteModel.baseUrlAt(i),
                "siteType": siteType,
                "premium": root.controller.apiSiteModel.premiumAt(i),
                "shareSelected": root.controller.apiSiteModel.shareSelectedAt(i),
                "accessStatus": root.controller.apiSiteModel.accessStatusAt(i),
                "accessStatusText": root.controller.apiSiteModel.accessStatusTextAt(i)
            })
        }
        Qt.callLater(function() {
            var maximumY = Math.max(0, siteList.contentHeight - siteList.height)
            siteList.contentY = Math.max(0, Math.min(maximumY, previousContentY))
        })
    }

    function clampSiteListContentY() {
        Qt.callLater(function() {
            var maximumY = Math.max(0, siteList.contentHeight - siteList.height)
            siteList.contentY = Math.max(0, Math.min(maximumY, siteList.contentY))
        })
    }

    function filteredIndexForSource(sourceIndex) {
        for (var i = 0; i < filteredSiteList.count; ++i) {
            if (filteredSiteList.get(i).sourceIndex === sourceIndex) return i
        }
        return -1
    }

    function syncFilteredSite(sourceIndex) {
        if (sourceIndex < 0 || sourceIndex >= root.controller.apiSiteModel.count) return
        var siteType = root.controller.apiSiteModel.typeAt(sourceIndex)
        var matches = root.controller.apiSiteModel.matchesFilter(sourceIndex, siteSearchInput.text)
                      && root.matchesTypeFilter(siteType)
        var filteredIndex = root.filteredIndexForSource(sourceIndex)
        if (!matches) {
            if (filteredIndex >= 0) filteredSiteList.remove(filteredIndex)
            return
        }

        var siteData = {
            "sourceIndex": sourceIndex,
            "name": root.controller.apiSiteModel.nameAt(sourceIndex),
            "baseUrl": root.controller.apiSiteModel.baseUrlAt(sourceIndex),
            "siteType": siteType,
            "premium": root.controller.apiSiteModel.premiumAt(sourceIndex),
            "shareSelected": root.controller.apiSiteModel.shareSelectedAt(sourceIndex),
            "accessStatus": root.controller.apiSiteModel.accessStatusAt(sourceIndex),
            "accessStatusText": root.controller.apiSiteModel.accessStatusTextAt(sourceIndex)
        }
        if (filteredIndex >= 0) {
            for (var key in siteData) filteredSiteList.setProperty(filteredIndex, key, siteData[key])
            return
        }

        var insertIndex = 0
        while (insertIndex < filteredSiteList.count
               && filteredSiteList.get(insertIndex).sourceIndex < sourceIndex) ++insertIndex
        filteredSiteList.insert(insertIndex, siteData)
    }

    Connections {
        target: root.controller.apiSiteModel
        function onCountChanged() { root.refreshFilteredSites() }
        function onDataChanged(topLeft, bottomRight) {
            for (var row = topLeft.row; row <= bottomRight.row; ++row) root.syncFilteredSite(row)
        }
        function onOrderChanged() { root.refreshFilteredSites() }
        function onRemoteSitesLoadFinished(message) { root.message = message }
        function onJsonSitesLoadFinished(message) { root.message = message }
    }

    component ActionButton: Rectangle {
        id: button
        property string label: ""
        property color fillColor: root.theme.surfaceColor
        property color hoverColor: root.theme.panelRaisedColor
        property color labelColor: root.theme.textSecondaryColor
        property bool outlined: true
        property bool enabledState: true
        signal clicked()

        Layout.preferredWidth: Math.max(78, buttonText.implicitWidth + 24)
        Layout.preferredHeight: 32
        radius: root.theme.controlRadius
        color: buttonMouse.containsMouse && enabledState ? hoverColor : fillColor
        border.color: root.theme.borderColor
        border.width: outlined ? 1 : 0
        opacity: enabledState ? 1.0 : 0.45

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            enabled: button.enabledState
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }

        Text {
            id: buttonText
            anchors.centerIn: parent
            text: button.label
            color: button.labelColor
            font.pixelSize: 12
            font.bold: true
            font.family: root.theme.fontFamily
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: theme.gap

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            radius: theme.panelRadius
            color: theme.panelColor
            border.color: theme.subtleBorderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: theme.edgePadding
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: "站点管理"
                        color: theme.textPrimaryColor
                        font.family: theme.fontFamily
                        font.pixelSize: theme.sectionTitleSize
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "管理视频 / 图片 / 短视频 API 站点，支持检测、筛选和加密分享。"
                        color: theme.textMutedColor
                        font.family: theme.fontFamily
                        font.pixelSize: theme.captionSize
                        elide: Text.ElideRight
                    }
                }

                ActionButton {
                    label: root.controller.apiSiteModel.remoteSitesLoading ? "加载中..." : "加载站点"
                    enabledState: !root.controller.apiSiteModel.remoteSitesLoading
                                  && !root.controller.apiSiteModel.jsonSitesLoading
                    onClicked: {
                        root.message = "正在加载远程站点..."
                        root.controller.apiSiteModel.loadRemoteSites()
                    }
                }

                ActionButton {
                    label: "检测全部"
                    fillColor: theme.accentColor
                    hoverColor: theme.accentMutedColor
                    labelColor: "#ffffff"
                    outlined: false
                    onClicked: {
                        root.controller.apiSiteModel.refreshAllSiteStatuses()
                        root.message = "正在检测全部站点..."
                    }
                }

                ActionButton {
                    label: "剪贴板导入"
                    onClicked: root.message = root.controller.apiSiteModel.importSitesFromClipboard()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.gap

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 520
                radius: theme.panelRadius
                color: theme.panelColor
                border.color: theme.subtleBorderColor
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.edgePadding
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "站点列表"
                            color: theme.textPrimaryColor
                            font.family: theme.fontFamily
                            font.pixelSize: theme.bodySize
                            font.bold: true
                        }

                        ActionButton {
                            label: "全选分享"
                            onClicked: {
                                root.controller.apiSiteModel.selectAllForShare(true)
                                root.message = "已全选分享站点"
                            }
                        }

                        ActionButton {
                            label: "清空选择"
                            onClicked: {
                                root.controller.apiSiteModel.selectAllForShare(false)
                                root.message = "已清空分享选择"
                            }
                        }

                        ActionButton {
                            label: "删除选中"
                            fillColor: theme.dangerColor
                            hoverColor: theme.dangerColor
                            labelColor: "#ffffff"
                            outlined: false
                            onClicked: {
                                root.message = root.controller.apiSiteModel.removeSelectedSites()
                                root.resetForm()
                            }
                        }

                        ActionButton {
                            label: "加密复制"
                            fillColor: theme.accentColor
                            hoverColor: theme.accentMutedColor
                            labelColor: "#ffffff"
                            outlined: false
                            onClicked: root.message = root.controller.apiSiteModel.shareSelectedToClipboard()
                        }

                        ActionButton {
                            label: "URL 去重"
                            onClicked: root.message = root.controller.apiSiteModel.deduplicateByUrl()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: siteSearchInput
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            placeholderText: "按名称或 URL 搜索站点"
                            color: theme.textPrimaryColor
                            font.family: theme.fontFamily
                            font.pixelSize: 13
                            background: Rectangle {
                                radius: 8
                                color: theme.surfaceColor
                                border.color: siteSearchInput.activeFocus ? theme.accentColor : theme.borderColor
                                border.width: 1
                            }
                            onTextChanged: root.refreshFilteredSites()
                        }

                        ComboBox {
                            id: typeFilter
                            Layout.preferredWidth: 136
                            Layout.preferredHeight: 36
                            model: [
                                { text: "全部类型", value: "all" },
                                { text: "视频", value: "video" },
                                { text: "短视频", value: "shortvideo" },
                                { text: "图片", value: "image" }
                            ]
                            textRole: "text"
                            valueRole: "value"
                            font.family: theme.fontFamily
                            font.pixelSize: 13
                            background: Rectangle {
                                radius: 8
                                color: theme.surfaceColor
                                border.color: typeFilter.activeFocus ? theme.accentColor : theme.borderColor
                                border.width: 1
                            }
                            onCurrentIndexChanged: root.refreshFilteredSites()
                        }
                    }

                    ListView {
                        id: siteList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: filteredSiteList
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.vertical: ScrollBar {
                            policy: siteList.contentHeight > siteList.height
                                ? ScrollBar.AsNeeded
                                : ScrollBar.AlwaysOff
                        }

                        delegate: Rectangle {
                            id: siteRow
                            width: ListView.view.width
                            height: 82
                            radius: theme.controlRadius
                            color: rowHover.hovered ? theme.panelRaisedColor : theme.surfaceColor
                            border.color: root.controller.apiSiteModel.currentIndex === model.sourceIndex ? theme.accentColor : theme.subtleBorderColor
                            border.width: 1
                            z: root.reorderSourceIndex === model.sourceIndex ? 10 : 0
                            opacity: root.reorderSourceIndex === model.sourceIndex ? 0.88 : 1.0

                            transform: Translate {
                                y: root.reorderSourceIndex === model.sourceIndex
                                   ? root.reorderVisualOffset(siteRow.y)
                                   : 0
                            }

                            HoverHandler { id: rowHover }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Rectangle {
                                    id: reorderHandle
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 36
                                    radius: theme.controlRadius
                                    color: root.reorderSourceIndex === model.sourceIndex
                                           ? theme.accentMutedColor
                                           : "transparent"
                                    opacity: root.sortingEnabled ? 1.0 : 0.35

                                    Text {
                                        anchors.centerIn: parent
                                        text: "☰"
                                        color: root.sortingEnabled ? theme.textSecondaryColor : theme.textMutedColor
                                        font.pixelSize: 18
                                        font.family: theme.fontFamily
                                    }

                                    ToolTip.visible: reorderMouse.containsMouse
                                    ToolTip.text: root.sortingEnabled ? "拖动调整站点顺序" : "清除搜索和类型筛选后可排序"

                                    MouseArea {
                                        id: reorderMouse
                                        anchors.fill: parent
                                        enabled: root.sortingEnabled
                                        hoverEnabled: true
                                        preventStealing: true
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        onPressed: function(mouse) {
                                            var point = reorderHandle.mapToItem(siteList, mouse.x, mouse.y)
                                            root.beginSiteReorder(model.sourceIndex, point.y, siteRow.y)
                                        }
                                        onPositionChanged: function(mouse) {
                                            if (!pressed) return
                                            var point = reorderHandle.mapToItem(siteList, mouse.x, mouse.y)
                                            root.updateSiteReorder(point.y)
                                        }
                                        onReleased: root.finishSiteReorder(false)
                                        onCanceled: root.finishSiteReorder(true)
                                    }
                                }

                                CheckBox {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    checked: model.shareSelected
                                    onToggled: root.controller.apiSiteModel.setShareSelected(model.sourceIndex, checked)
                                }

                                Rectangle {
                                    Layout.preferredWidth: 10
                                    Layout.fillHeight: true
                                    Layout.topMargin: 14
                                    Layout.bottomMargin: 14
                                    radius: 5
                                    color: root.statusColor(model.accessStatus)
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            text: model.name
                                            color: theme.textPrimaryColor
                                            font.family: theme.fontFamily
                                            font.pixelSize: 14
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            visible: model.premium
                                            Layout.preferredWidth: premiumText.implicitWidth + 16
                                            Layout.preferredHeight: 22
                                            radius: 6
                                            color: "#4a3d18"
                                            border.color: "#d8ad3c"
                                            border.width: 1

                                            Text {
                                                id: premiumText
                                                anchors.centerIn: parent
                                                text: "优质"
                                                color: "#ffd86b"
                                                font.family: theme.fontFamily
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: Math.max(48, typeText.implicitWidth + 18)
                                            Layout.preferredHeight: 22
                                            radius: 11
                                            color: root.siteTypeFillColor(model.siteType)
                                            border.color: root.siteTypeBorderColor(model.siteType)
                                            border.width: 1

                                            Text {
                                                id: typeText
                                                anchors.centerIn: parent
                                                text: root.siteTypeLabel(model.siteType)
                                                color: root.siteTypeTextColor(model.siteType)
                                                font.family: theme.fontFamily
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: Math.max(80, statusText.implicitWidth + 18)
                                            Layout.preferredHeight: 24
                                            radius: 12
                                            color: theme.surfaceColor
                                            border.color: root.statusColor(model.accessStatus)
                                            border.width: 1

                                            Text {
                                                id: statusText
                                                anchors.centerIn: parent
                                                text: model.accessStatusText
                                                color: root.statusColor(model.accessStatus)
                                                font.family: theme.fontFamily
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: model.baseUrl
                                        color: theme.textMutedColor
                                        font.family: theme.fontFamily
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }

                                ActionButton {
                                    label: model.premium ? "★" : "☆"
                                    Layout.preferredWidth: 36
                                    labelColor: model.premium ? "#ffd86b" : theme.textSecondaryColor
                                    onClicked: {
                                        var wasPremium = model.premium
                                        root.controller.apiSiteModel.togglePremium(model.sourceIndex)
                                        root.message = wasPremium ? "已取消优质站点" : "已标记为优质站点"
                                    }
                                }

                                ActionButton {
                                    label: "选中"
                                    enabledState: root.controller.apiSiteModel.currentIndex !== model.sourceIndex
                                    onClicked: {
                                        root.controller.apiSiteModel.selectAt(model.sourceIndex)
                                        root.message = "已切换到：" + model.name
                                    }
                                }

                                ActionButton {
                                    label: "检测"
                                    onClicked: root.controller.apiSiteModel.refreshSiteStatusAt(model.sourceIndex)
                                }

                                ActionButton {
                                    label: "编辑"
                                    onClicked: {
                                        root.editIndex = model.sourceIndex
                                        nameInput.text = model.name
                                        urlInput.text = model.baseUrl
                                        typeInput.currentIndex = root.siteTypeInputIndex(model.siteType)
                                    }
                                }

                                ActionButton {
                                    label: "删除"
                                    enabledState: root.controller.apiSiteModel.count > 1
                                    fillColor: theme.dangerColor
                                    hoverColor: theme.dangerColor
                                    labelColor: "#ffffff"
                                    outlined: false
                                    onClicked: {
                                        root.controller.apiSiteModel.removeAt(model.sourceIndex)
                                        root.resetForm()
                                        root.message = "已删除站点"
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: filteredSiteList.count === 0
                            text: "暂无站点"
                            color: theme.textMutedColor
                            font.family: theme.fontFamily
                            font.pixelSize: theme.bodySize
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                radius: theme.panelRadius
                color: theme.panelColor
                border.color: theme.subtleBorderColor
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.edgePadding
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: root.isEditing ? "编辑站点" : "新增站点"
                        color: theme.textPrimaryColor
                        font.family: theme.fontFamily
                        font.pixelSize: theme.bodySize
                        font.bold: true
                    }

                    TextField {
                        id: nameInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        placeholderText: "站点名称"
                        color: theme.textPrimaryColor
                        font.family: theme.fontFamily
                        font.pixelSize: 13
                        background: Rectangle {
                            radius: 8
                            color: theme.surfaceColor
                            border.color: nameInput.activeFocus ? theme.accentColor : theme.borderColor
                            border.width: 1
                        }
                    }

                    TextArea {
                        id: urlInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        placeholderText: "https://example.com/api.php/provide/vod"
                        color: theme.textPrimaryColor
                        wrapMode: TextEdit.WrapAnywhere
                        font.family: theme.fontFamily
                        font.pixelSize: 13
                        background: Rectangle {
                            radius: 8
                            color: theme.surfaceColor
                            border.color: urlInput.activeFocus ? theme.accentColor : theme.borderColor
                            border.width: 1
                        }
                    }

                    ComboBox {
                        id: typeInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        model: [
                            { text: "视频站点", value: "video" },
                            { text: "图片站点", value: "image" },
                            { text: "短视频站点", value: "shortvideo" }
                        ]
                        textRole: "text"
                        valueRole: "value"
                        font.family: theme.fontFamily
                        font.pixelSize: 13
                        background: Rectangle {
                            radius: 8
                            color: theme.surfaceColor
                            border.color: typeInput.activeFocus ? theme.accentColor : theme.borderColor
                            border.width: 1
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ActionButton {
                            label: root.isEditing ? "保存" : "添加"
                            fillColor: theme.accentColor
                            hoverColor: theme.accentMutedColor
                            labelColor: "#ffffff"
                            outlined: false
                            onClicked: {
                                var n = nameInput.text.trim()
                                var u = urlInput.text.trim()
                                if (n.length === 0 || u.length === 0) {
                                    root.message = "请填写站点名称和地址"
                                    return
                                }

                                if (root.isEditing) {
                                    root.controller.apiSiteModel.update(root.editIndex, n, u, root.siteTypeFromInputIndex(typeInput.currentIndex))
                                    root.message = "已保存站点"
                                } else {
                                    root.controller.apiSiteModel.add(n, u, root.siteTypeFromInputIndex(typeInput.currentIndex))
                                    root.controller.apiSiteModel.selectAt(root.controller.apiSiteModel.count - 1)
                                    root.message = "已添加并选中站点"
                                }
                                root.resetForm()
                            }
                        }

                        ActionButton {
                            label: "取消"
                            enabledState: root.isEditing || nameInput.text.length > 0 || urlInput.text.length > 0
                            onClicked: root.resetForm()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.subtleBorderColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "提示"
                        color: theme.textPrimaryColor
                        font.family: theme.fontFamily
                        font.pixelSize: theme.bodySize
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "检测会请求站点接口，只判断是否能连通并返回正常 HTTP 状态。"
                        color: theme.textMutedColor
                        wrapMode: Text.WordWrap
                        font.family: theme.fontFamily
                        font.pixelSize: theme.captionSize
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.message.length > 0
                        text: root.message
                        color: theme.accentColor
                        wrapMode: Text.WordWrap
                        font.family: theme.fontFamily
                        font.pixelSize: theme.captionSize
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    Component.onCompleted: {
        root.refreshFilteredSites()
        if (root.controller.apiSiteModel.hasShareContentInClipboard()) {
            root.message = "剪贴板里检测到 MeloBox 加密站点分享，可直接导入"
        }
    }
}

