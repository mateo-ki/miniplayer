import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    property var strings
    property bool active: false
    property real hostWidth: width
    property real hostHeight: height
    signal saveUiSettingRequested(string key, var value)
    signal navigateRequested(int pageIndex)
    property bool initialImageRequested: false
    property string refreshMode: "same"
    property int autoRefreshMs: 0
    property int imagePrefetchPending: 0
    readonly property int imagePoolTargetSize: 8
    readonly property int imagePrefetchConcurrency: 2
    ListModel { id: imageSiteList }
    ListModel { id: imagePrefetchPool }
    onRefreshModeChanged: root.saveUiSettingRequested("imageRefreshMode", refreshMode)
    onAutoRefreshMsChanged: root.saveUiSettingRequested("imageAutoRefreshMs", autoRefreshMs)

    function rebuildImageSites() {
        imageSiteList.clear()
        for (var i = 0; i < root.controller.apiSiteModel.count; ++i) {
            if (root.controller.apiSiteModel.typeAt(i) !== "image") continue
            imageSiteList.append({
                "sourceIndex": i,
                "siteName": root.controller.apiSiteModel.nameAt(i),
                "siteBaseUrl": root.controller.apiSiteModel.baseUrlAt(i)
            })
        }
    }

    function preferredImageSiteComboIndex() {
        var savedImageUrl = root.controller.uiSetting("selectedImageSiteUrl", "")
        if (savedImageUrl && savedImageUrl.length > 0) {
            for (var savedIndex = 0; savedIndex < imageSiteList.count; ++savedIndex) {
                if (imageSiteList.get(savedIndex).siteBaseUrl === savedImageUrl)
                    return savedIndex
            }
        }
        var current = root.controller.apiSiteModel.currentIndex
        for (var i = 0; i < imageSiteList.count; ++i) {
            if (imageSiteList.get(i).sourceIndex === current) {
                return i
            }
        }
        for (var j = 0; j < imageSiteList.count; ++j) {
            if (imageSiteList.get(j).siteBaseUrl.indexOf("t.alcy.cc") < 0) return j
        }
        return imageSiteList.count > 0 ? 0 : -1
    }

    function syncImageSiteMenuIndex() {
        var comboIndex = root.preferredImageSiteComboIndex()
        if (imageSiteMenu.currentIndex !== comboIndex) {
            imageSiteMenu.currentIndex = comboIndex
        }
    }

    function selectPreferredImageSite() {
        root.rebuildImageSites()
        var comboIndex = root.preferredImageSiteComboIndex()
        if (comboIndex >= 0) {
            root.controller.apiSiteModel.selectAt(imageSiteList.get(comboIndex).sourceIndex)
            imageSiteMenu.currentIndex = comboIndex
        }
    }

    function saveSelectedImageSite(comboIndex) {
        if (comboIndex < 0 || comboIndex >= imageSiteList.count)
            return
        root.saveUiSettingRequested("selectedImageSiteUrl", imageSiteList.get(comboIndex).siteBaseUrl)
    }

    function currentImageApiUrl() {
        var comboIndex = imageSiteMenu.currentIndex
        if (comboIndex < 0 || comboIndex >= imageSiteList.count)
            comboIndex = root.preferredImageSiteComboIndex()
        if (comboIndex < 0 || comboIndex >= imageSiteList.count)
            return ""
        return imageSiteList.get(comboIndex).siteBaseUrl
    }

    function resetImagePrefetchPool() {
        imagePrefetchPool.clear()
        imagePrefetchPending = 0
    }
    function consumePrefetchedImage(apiUrl) {
        for (var i = 0; i < imagePrefetchPool.count; ++i) {
            var item = imagePrefetchPool.get(i)
            if (item.apiUrl !== apiUrl)
                continue
            var imageUrl = item.imageUrl
            imagePrefetchPool.remove(i)
            root.controller.showPrefetchedImage(imageUrl)
            Qt.callLater(root.refillImagePrefetchPool)
            return true
        }
        return false
    }

    function refillImagePrefetchPool() {
        if (root.controller.imageLoading)
            return
        var apiUrl = root.currentImageApiUrl()
        if (!apiUrl || apiUrl.length === 0)
            return
        while (imagePrefetchPool.count + imagePrefetchPending < imagePoolTargetSize
                && imagePrefetchPending < imagePrefetchConcurrency) {
            imagePrefetchPending += 1
            root.controller.prefetchImage(apiUrl)
        }
    }
    function requestRandomImage(allowSourceSwitch) {
        if (root.controller.imageLoading)
            return
        if (allowSourceSwitch !== false
                && refreshMode === "different"
                && imageSiteList.count > 1) {
            var currentCombo = preferredImageSiteComboIndex()
            var nextCombo = currentCombo >= 0 ? (currentCombo + 1) % imageSiteList.count : 0
            root.controller.apiSiteModel.selectAt(imageSiteList.get(nextCombo).sourceIndex)
            imageSiteMenu.currentIndex = nextCombo
            root.saveSelectedImageSite(nextCombo)
            root.resetImagePrefetchPool()
        }
        initialImageRequested = true
        var apiUrl = root.currentImageApiUrl()
        if (!root.consumePrefetchedImage(apiUrl))
            root.controller.loadRandomImage(apiUrl)
        Qt.callLater(root.refillImagePrefetchPool)
    }

    function ensureInitialImage() {
        root.selectPreferredImageSite()
        if (initialImageRequested
                || root.controller.imageLoading
                || root.controller.currentImageDisplayUrl.length > 0) {
            return
        }
        root.requestRandomImage()
    }

    function autoRefreshIndexForMs(ms) {
        if (ms === 1000) return 1
        if (ms === 3000) return 2
        if (ms === 5000) return 3
        return 0
    }

    function autoRefreshMsForIndex(index) {
        if (index === 1) return 1000
        if (index === 2) return 3000
        if (index === 3) return 5000
        return 0
    }

    Timer {
        id: imagePrefetchRetryTimer
        interval: 750
        repeat: false
        onTriggered: root.refillImagePrefetchPool()
    }

    Timer {
        id: imageAutoRefreshTimer
        interval: Math.max(250, root.autoRefreshMs)
        repeat: true
        running: root.active && root.autoRefreshMs > 0
        onTriggered: {
            if (!root.controller.imageLoading)
                root.requestRandomImage()
        }
    }

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
            clip: true

            Item {
                anchors.fill: parent
                anchors.margins: root.theme.edgePadding

                Rectangle {
                    id: imageControlDock
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: imageStatus.top
                    anchors.bottomMargin: imageStatus.visible ? 8 : 0
                    height: 58
                    radius: 8
                    color: root.theme.surfaceColor
                    border.color: root.theme.borderColor
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            RowLayout {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: imagePrefetchPool.count > 0
                                        ? root.theme.accentColor : root.theme.textMutedColor
                                }

                                Column {
                                    spacing: 2

                                    Text {
                                        text: imageSiteMenu.currentIndex >= 0
                                            && imageSiteMenu.currentIndex < imageSiteList.count
                                            ? imageSiteList.get(imageSiteMenu.currentIndex).siteName
                                            : root.strings.imageTitle
                                        color: root.theme.textPrimaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.bodySize
                                        font.bold: true
                                        elide: Text.ElideRight
                                        width: Math.min(220, imageControlDock.width * 0.24)
                                    }

                                    Text {
                                        text: "预载 " + imagePrefetchPool.count + "/" + root.imagePoolTargetSize
                                        color: root.theme.textMutedColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.captionSize
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 112
                            Layout.preferredHeight: 38
                            radius: root.theme.controlRadius
                            color: imageChangeHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor
                            opacity: root.controller.imageLoading ? 0.58 : 1.0

                            HoverHandler { id: imageChangeHover }

                            MouseArea {
                                anchors.fill: parent
                                enabled: !root.controller.imageLoading
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.requestRandomImage()
                            }

                            Row {
                                anchors.centerIn: parent
                                spacing: 7

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "\uE72C"
                                    color: "#ffffff"
                                    font.family: "Segoe Fluent Icons"
                                    font.pixelSize: 15
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.strings.imageChangeLabel
                                    color: "#ffffff"
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Row {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                ToolButton {
                                    width: 36
                                    height: 36
                                    enabled: root.controller.currentImageUrl.length > 0
                                        && randomImage.status === Image.Ready
                                        && !root.controller.imageLoading
                                    opacity: enabled ? 1.0 : 0.38
                                    onClicked: root.controller.copyCurrentImage()
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.strings.copyLabel
                                    background: Rectangle {
                                        radius: root.theme.controlRadius
                                        color: parent.hovered ? root.theme.panelRaisedColor : "transparent"
                                    }
                                    contentItem: Text {
                                        text: "\uE8C8"
                                        color: root.theme.textSecondaryColor
                                        font.family: "Segoe Fluent Icons"
                                        font.pixelSize: 16
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                ToolButton {
                                    width: 36
                                    height: 36
                                    enabled: root.controller.currentImageUrl.length > 0
                                        && randomImage.status === Image.Ready
                                        && !root.controller.imageLoading
                                    opacity: enabled ? 1.0 : 0.38
                                    onClicked: root.controller.saveCurrentImage()
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.strings.downloadLabel
                                    background: Rectangle {
                                        radius: root.theme.controlRadius
                                        color: parent.hovered ? root.theme.panelRaisedColor : "transparent"
                                    }
                                    contentItem: Text {
                                        text: "\uE896"
                                        color: root.theme.textSecondaryColor
                                        font.family: "Segoe Fluent Icons"
                                        font.pixelSize: 16
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                ToolButton {
                                    width: 36
                                    height: 36
                                    checked: imageSettingsPopup.opened
                                    onClicked: imageSettingsPopup.opened
                                        ? imageSettingsPopup.close() : imageSettingsPopup.open()
                                    ToolTip.visible: hovered
                                    ToolTip.text: "图片设置"
                                    background: Rectangle {
                                        radius: root.theme.controlRadius
                                        color: parent.hovered || imageSettingsPopup.opened
                                            ? root.theme.panelRaisedColor : "transparent"
                                    }
                                    contentItem: Text {
                                        text: "\uE713"
                                        color: imageSettingsPopup.opened
                                            ? root.theme.accentColor : root.theme.textSecondaryColor
                                        font.family: "Segoe Fluent Icons"
                                        font.pixelSize: 16
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }
                }

                Popup {
                    id: imageSettingsPopup
                    parent: Overlay.overlay
                    x: Math.max(16, root.hostWidth - width - root.theme.edgePadding - 12)
                    y: Math.max(16, root.hostHeight - height - imageControlDock.height
                        - root.theme.edgePadding - 28)
                    width: Math.min(360, root.hostWidth - 32)
                    padding: 0
                    modal: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                    background: Rectangle {
                        radius: 8
                        color: root.theme.panelRaisedColor
                        border.color: root.theme.borderColor
                        border.width: 1
                    }

                    contentItem: ColumnLayout {
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 10
                            Layout.topMargin: 12
                            Layout.bottomMargin: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: "图片设置"
                                    color: root.theme.textPrimaryColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }

                                Text {
                                    text: "来源、换图规则与缓存"
                                    color: root.theme.textMutedColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                }
                            }

                            ToolButton {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                onClicked: imageSettingsPopup.close()
                                ToolTip.visible: hovered
                                ToolTip.text: "关闭"
                                contentItem: Text {
                                    text: "\uE711"
                                    color: root.theme.textSecondaryColor
                                    font.family: "Segoe Fluent Icons"
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: root.theme.subtleBorderColor
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.margins: 16
                            spacing: 14

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 7

                                Text {
                                    text: "图片来源"
                                    color: root.theme.textMutedColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                }

                                ComboBox {
                                    id: imageSiteMenu
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 36
                                    model: imageSiteList
                                    textRole: "siteName"
                                    currentIndex: -1
                                    onActivated: function(index) {
                                        if (index < 0 || index >= imageSiteList.count) return
                                        currentIndex = index
                                        root.controller.apiSiteModel.selectAt(imageSiteList.get(index).sourceIndex)
                                        root.saveSelectedImageSite(index)
                                        root.resetImagePrefetchPool()
                                        root.requestRandomImage(false)
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 7

                                Text {
                                    text: root.strings.imageRefreshModeLabel
                                    color: root.theme.textMutedColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: root.theme.captionSize
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Button {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        text: root.strings.imageSameSourceRefresh
                                        checkable: true
                                        checked: root.refreshMode === "same"
                                        onClicked: root.refreshMode = "same"
                                    }

                                    Button {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        text: root.strings.imageSwitchSourceRefresh
                                        checkable: true
                                        checked: root.refreshMode === "different"
                                        onClicked: root.refreshMode = "different"
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Text {
                                        text: root.strings.imageAutoRefreshLabel
                                        color: root.theme.textPrimaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.bodySize
                                    }

                                    Text {
                                        text: root.autoRefreshMs > 0
                                            ? "按间隔自动换图" : "保持当前图片"
                                        color: root.theme.textMutedColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.captionSize
                                    }
                                }

                                ComboBox {
                                    id: imageAutoRefreshMenu
                                    Layout.preferredWidth: 104
                                    Layout.preferredHeight: 34
                                    model: ["关闭", "1秒", "3秒", "5秒"]
                                    currentIndex: root.autoRefreshIndexForMs(root.autoRefreshMs)
                                    onActivated: function(index) {
                                        root.autoRefreshMs = root.autoRefreshMsForIndex(index)
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: root.theme.subtleBorderColor
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Text {
                                        text: "图片缓存"
                                        color: root.theme.textPrimaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.bodySize
                                    }

                                    Text {
                                        text: "内存预载 " + imagePrefetchPool.count + "/" + root.imagePoolTargetSize
                                            + " · 磁盘 " + root.controller.imageDiskCacheCount
                                            + " 张 · " + Math.round(root.controller.imageDiskCacheBytes / 1048576) + " MB"
                                        color: root.theme.textMutedColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.captionSize
                                        elide: Text.ElideRight
                                    }
                                }

                                Button {
                                    Layout.preferredHeight: 32
                                    text: "清理缓存"
                                    enabled: root.controller.imageDiskCacheCount > 0
                                    onClicked: {
                                        root.controller.clearImageCache()
                                        root.resetImagePrefetchPool()
                                        root.refillImagePrefetchPool()
                                    }
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                text: root.strings.siteLabel
                                onClicked: {
                                    imageSettingsPopup.close()
                                    root.navigateRequested(9)
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    id: imageViewport
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: imageControlDock.top
                    anchors.bottomMargin: 12
                    radius: root.theme.panelRadius
                    color: root.theme.surfaceColor
                    border.color: root.theme.subtleBorderColor
                    border.width: 1
                    clip: true

                    Image {
                        id: randomImage
                        anchors.fill: parent
                        anchors.margins: 12
                        source: root.controller.currentImageDisplayUrl
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        visible: root.controller.currentImageDisplayUrl.length > 0
                        onSourceChanged: console.log("[ImageDebug][QML] source=", source)
                        onStatusChanged: console.log("[ImageDebug][QML] status=", status, "source=", source, "painted=", paintedWidth + "x" + paintedHeight)
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: randomImage.status === Image.Ready
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
                        onDoubleClicked: imagePreviewDialog.open()
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: root.controller.imageLoading
                        visible: running
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width * 0.72
                        visible: (root.controller.currentImageDisplayUrl.length === 0 || randomImage.status === Image.Error)
                            && !root.controller.imageLoading
                        text: root.controller.imageMessage.length > 0
                            ? root.controller.imageMessage
                            : root.strings.imageHint
                        color: root.theme.textMutedColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    id: imageStatus
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: visible ? implicitHeight : 0
                    visible: root.controller.imageMessage.length > 0
                    text: root.controller.imageMessage
                    color: root.controller.imageMessage.indexOf("失败") >= 0 || root.controller.imageMessage.indexOf("无效") >= 0
                        ? root.theme.dangerColor
                        : root.theme.accentColor
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.captionSize
                    elide: Text.ElideRight
                }
            }
        }
    }

    Dialog {
        id: imagePreviewDialog
        anchors.centerIn: parent
        width: Math.max(320, Math.min(root.width - 48, 1200))
        height: Math.max(240, Math.min(root.height - 48, 820))
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0

        background: Rectangle {
            color: "#ee090909"
            border.color: root.theme.borderColor
            border.width: 1
            radius: root.theme.panelRadius
        }

        contentItem: Image {
            source: root.controller.currentImageDisplayUrl
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
        }
    }

    Component.onCompleted: {
        root.refreshMode = root.controller.uiSetting("imageRefreshMode", root.refreshMode)
        root.autoRefreshMs = root.controller.uiSetting("imageAutoRefreshMs", root.autoRefreshMs)
        root.selectPreferredImageSite()
        root.refillImagePrefetchPool()
    }

    Connections {
        target: root.controller
        function onImagePrefetched(apiUrl, imageUrl) {
            if (apiUrl !== root.currentImageApiUrl())
                return

            root.imagePrefetchPending = Math.max(0, root.imagePrefetchPending - 1)
            if (imageUrl && imageUrl.length > 0) {
                imagePrefetchPool.append({
                    "apiUrl": apiUrl,
                    "imageUrl": imageUrl
                })
                Qt.callLater(root.refillImagePrefetchPool)
            } else {
                imagePrefetchRetryTimer.restart()
            }
        }
        function onImageStateChanged() {
            if (!root.controller.imageLoading)
                Qt.callLater(root.refillImagePrefetchPool)
        }
    }

    Connections {
        target: root.controller.apiSiteModel
        function onCountChanged() {
            root.rebuildImageSites()
            root.syncImageSiteMenuIndex()
        }
        function onDataChanged() {
            root.rebuildImageSites()
            root.syncImageSiteMenuIndex()
        }
        function onCurrentIndexChanged() {
            root.rebuildImageSites()
            root.syncImageSiteMenuIndex()
        }
    }
}
