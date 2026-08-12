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
                "shareSelected": root.controller.apiSiteModel.shareSelectedAt(i),
                "accessStatus": root.controller.apiSiteModel.accessStatusAt(i),
                "accessStatusText": root.controller.apiSiteModel.accessStatusTextAt(i)
            })
        }
    }

    Connections {
        target: root.controller.apiSiteModel
        function onCountChanged() { root.refreshFilteredSites() }
        function onCurrentIndexChanged() { root.refreshFilteredSites() }
        function onDataChanged() { root.refreshFilteredSites() }
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
                            width: ListView.view.width
                            height: 82
                            radius: theme.controlRadius
                            color: rowHover.hovered ? theme.panelRaisedColor : theme.surfaceColor
                            border.color: root.controller.apiSiteModel.currentIndex === model.sourceIndex ? theme.accentColor : theme.subtleBorderColor
                            border.width: 1

                            HoverHandler { id: rowHover }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

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

