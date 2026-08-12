import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "API 站点管理"
    anchors.centerIn: parent
    modal: true
    width: 580
    height: 520
    property var theme

    property int editIndex: -1
    property bool isEditing: editIndex >= 0
    property string shareMessage: ""

    component PillButton: Rectangle {
        id: pill
        property string label: ""
        property color fillColor: root.theme.surfaceColor
        property color hoverColor: root.theme.panelRaisedColor
        property color labelColor: root.theme.textSecondaryColor
        property bool outlined: true
        signal clicked()

        Layout.preferredWidth: Math.max(76, pillLabel.implicitWidth + 24)
        Layout.preferredHeight: 30
        radius: 7
        color: pillMouse.containsMouse ? hoverColor : fillColor
        border.color: root.theme.borderColor
        border.width: outlined ? 1 : 0

        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }

        Text {
            id: pillLabel
            anchors.centerIn: parent
            text: pill.label
            color: pill.labelColor
            font.pixelSize: 12
            font.bold: true
            font.family: root.theme.fontFamily
        }
    }

    background: Rectangle {
        radius: theme.panelRadius
        color: theme.panelColor
        border.color: theme.borderColor
        border.width: 1
    }

    header: Rectangle {
        height: 48
        radius: theme.panelRadius
        color: theme.chromeColor

        Text {
            anchors.centerIn: parent
            text: "API 站点管理"
            color: theme.textPrimaryColor
            font.pixelSize: 16
            font.bold: true
            font.family: theme.fontFamily
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: playerController.apiSiteModel
            spacing: 4

            delegate: Rectangle {
                width: ListView.view.width
                height: 56
                radius: 6
                color: siteMouse.containsMouse ? theme.panelRaisedColor : theme.surfaceColor
                border.color: playerController.apiSiteModel.currentIndex === index ? theme.accentColor : theme.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    CheckBox {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        checked: model.shareSelected
                        onToggled: playerController.apiSiteModel.setShareSelected(index, checked)
                        ToolTip.visible: hovered
                        ToolTip.text: "勾选后可分享"
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        MouseArea {
                            id: siteMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: playerController.apiSiteModel.selectAt(index)
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            Text {
                                text: model.name
                                color: theme.textPrimaryColor
                                font.pixelSize: 13
                                font.bold: true
                                font.family: theme.fontFamily
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: model.baseUrl
                                color: theme.textMutedColor
                                font.pixelSize: 10
                                font.family: theme.fontFamily
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        color: editBtnMouse.containsMouse ? theme.panelRaisedColor : "transparent"

                        MouseArea {
                            id: editBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.editIndex = index
                                nameInput.text = model.name
                                urlInput.text = model.baseUrl
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "✎"
                            color: theme.textSecondaryColor
                            font.pixelSize: 14
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        color: delBtnMouse.containsMouse ? theme.dangerColor : "transparent"
                        visible: playerController.apiSiteModel.count > 1

                        MouseArea {
                            id: delBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: playerController.apiSiteModel.removeAt(index)
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: delBtnMouse.containsMouse ? "#ffffff" : theme.textMutedColor
                            font.pixelSize: 16
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            PillButton {
                label: "全选"
                onClicked: {
                    playerController.apiSiteModel.selectAllForShare(true)
                    root.shareMessage = "已全选站点"
                }
            }

            PillButton {
                label: "清空"
                onClicked: {
                    playerController.apiSiteModel.selectAllForShare(false)
                    root.shareMessage = "已清空选择"
                }
            }

            PillButton {
                label: "加密复制"
                fillColor: theme.accentColor
                hoverColor: theme.accentMutedColor
                labelColor: "#ffffff"
                outlined: false
                onClicked: root.shareMessage = playerController.apiSiteModel.shareSelectedToClipboard()
            }

            PillButton {
                label: "从剪贴板导入"
                onClicked: root.shareMessage = playerController.apiSiteModel.importSitesFromClipboard()
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            visible: root.shareMessage.length > 0
            text: root.shareMessage
            color: theme.textMutedColor
            font.pixelSize: 12
            font.family: theme.fontFamily
            elide: Text.ElideRight
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.borderColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: root.isEditing ? "编辑站点" : "添加站点"
                color: theme.textPrimaryColor
                font.pixelSize: 13
                font.bold: true
                font.family: theme.fontFamily
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: nameInput
                    Layout.preferredWidth: 120
                    placeholderText: "名称"
                    color: theme.textPrimaryColor
                    font.pixelSize: 12
                    font.family: theme.fontFamily
                    background: Rectangle {
                        radius: 4
                        color: theme.surfaceColor
                        border.color: nameInput.activeFocus ? theme.accentColor : theme.borderColor
                        border.width: 1
                    }
                }

                TextField {
                    id: urlInput
                    Layout.fillWidth: true
                    placeholderText: "https://example.com/provide/vod"
                    color: theme.textPrimaryColor
                    font.pixelSize: 12
                    font.family: theme.fontFamily
                    background: Rectangle {
                        radius: 4
                        color: theme.surfaceColor
                        border.color: urlInput.activeFocus ? theme.accentColor : theme.borderColor
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    radius: 6
                    color: addBtnMouse.containsMouse ? theme.accentMutedColor : theme.accentColor

                    MouseArea {
                        id: addBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var n = nameInput.text.trim()
                            var u = urlInput.text.trim()
                            if (n.length === 0 || u.length === 0) return
                            if (root.isEditing) {
                                playerController.apiSiteModel.update(root.editIndex, n, u)
                                root.editIndex = -1
                            } else {
                                playerController.apiSiteModel.add(n, u)
                            }
                            nameInput.text = ""
                            urlInput.text = ""
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: root.isEditing ? "保存" : "添加"
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                        font.family: theme.fontFamily
                    }
                }

                Rectangle {
                    visible: root.isEditing
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    radius: 6
                    color: cancelBtnMouse.containsMouse ? theme.panelRaisedColor : theme.surfaceColor
                    border.color: theme.borderColor
                    border.width: 1

                    MouseArea {
                        id: cancelBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.editIndex = -1
                            nameInput.text = ""
                            urlInput.text = ""
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        color: theme.textSecondaryColor
                        font.pixelSize: 12
                        font.family: theme.fontFamily
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    onOpened: {
        root.shareMessage = playerController.apiSiteModel.hasShareContentInClipboard()
                ? "剪贴板里检测到 MeloBox 加密站点分享，可直接导入"
                : ""
    }

    onClosed: {
        editIndex = -1
        shareMessage = ""
        nameInput.text = ""
        urlInput.text = ""
    }
}
