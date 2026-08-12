import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    property var strings
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
                            text: "鐞涖劍鍎忛崠"
                            color: root.theme.textPrimaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.sectionTitleSize
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.strings.memeDescription
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                            elide: Text.ElideRight
                        }
                    }

                    TextField {
                        id: memeSearchInput
                        Layout.preferredWidth: 220
                        Layout.preferredHeight: 36
                        text: root.strings.memeDragonLabel
                        placeholderText: root.strings.keywordPlaceholder
                        color: root.theme.textPrimaryColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: 13
                        background: Rectangle {
                            radius: 8
                            color: root.theme.surfaceColor
                            border.color: memeSearchInput.activeFocus ? root.theme.accentColor : root.theme.borderColor
                            border.width: 1
                        }
                        onAccepted: root.controller.searchMemes(text, 10)
                    }

                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 34
                        radius: root.theme.controlRadius
                        color: memeSearchHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor
                        opacity: root.controller.memeLoading ? 0.55 : 1.0

                        HoverHandler { id: memeSearchHover }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.controller.memeLoading
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.controller.searchMemes(memeSearchInput.text, 10)
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "鎼滅储"
                            color: "#ffffff"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            font.bold: true
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 34
                        radius: root.theme.controlRadius
                        color: dragonMemeHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor
                        border.color: root.theme.borderColor
                        border.width: 1

                        HoverHandler { id: dragonMemeHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.controller.loadDragonMeme()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: root.strings.memeDragonLabel
                            color: root.theme.textSecondaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            font.bold: true
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

                    GridView {
                        id: memeGrid
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        cellWidth: Math.max(180, width / Math.max(1, Math.floor(width / 220)))
                        cellHeight: 210
                        model: root.controller.memeImages

                        delegate: Rectangle {
                            width: memeGrid.cellWidth - 10
                            height: 198
                            radius: root.theme.controlRadius
                            color: memeHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor
                            border.color: memeHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor
                            border.width: 1
                            clip: true

                            HoverHandler { id: memeHover }

                            Image {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: memeActions.top
                                anchors.margins: 8
                                source: modelData
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                                z: 0
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                z: 1
                                onDoubleClicked: {
                                    memePreviewDialog.imageUrl = modelData
                                    memePreviewDialog.open()
                                }
                            }

                            RowLayout {
                                id: memeActions
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 8
                                spacing: 8
                                z: 9
                                height: 30

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    radius: 8
                                    color: memeCopyHover.hovered ? root.theme.accentMutedColor : "#cc202020"
                                    border.color: root.theme.borderColor
                                    border.width: 1

                                    HoverHandler { id: memeCopyHover }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.controller.copyMemeUrl(modelData)
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.strings.copyLabel
                                        color: root.theme.textPrimaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.captionSize
                                        font.bold: true
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    radius: 8
                                    color: memeSaveHover.hovered ? root.theme.accentMutedColor : "#cc202020"
                                    border.color: root.theme.borderColor
                                    border.width: 1

                                    HoverHandler { id: memeSaveHover }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.controller.saveMemeImage(modelData)
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.strings.downloadLabel
                                        color: root.theme.textPrimaryColor
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: root.theme.captionSize
                                        font.bold: true
                                    }
                                }
                            }
                        }
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: root.controller.memeLoading
                        visible: running
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width * 0.72
                        visible: root.controller.memeImages.length === 0 && !root.controller.memeLoading
                        text: root.strings.memeHint
                        color: root.theme.textMutedColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.controller.memeMessage.length > 0
                    text: root.controller.memeMessage
                    color: root.controller.memeMessage.indexOf("澶辫触") >= 0 || root.controller.memeMessage.indexOf("娌℃湁") >= 0
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
        if (root.controller.memeImages.length === 0) {
            root.controller.searchMemes(root.strings.memeDragonLabel, 10)
        }
    }

    Dialog {
        id: memePreviewDialog
        property string imageUrl: ""
        modal: true
        title: root.strings.memePreviewTitle
        width: Math.min(root.width * 0.86, 920)
        height: Math.min(root.height * 0.86, 720)
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.Close

        contentItem: Rectangle {
            color: "#101010"
            radius: root.theme.panelRadius
            border.color: root.theme.subtleBorderColor
            border.width: 1

            Image {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: previewActions.top
                anchors.margins: 14
                source: memePreviewDialog.imageUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }

            RowLayout {
                id: previewActions
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: 32
                    radius: root.theme.controlRadius
                    color: previewCopyHover.hovered ? root.theme.accentMutedColor : root.theme.surfaceColor
                    border.color: root.theme.borderColor
                    border.width: 1
                    HoverHandler { id: previewCopyHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.controller.copyMemeUrl(memePreviewDialog.imageUrl)
                    }
                    Text {
                        anchors.centerIn: parent
                        text: root.strings.copyLabel
                        color: root.theme.textSecondaryColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                        font.bold: true
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: 32
                    radius: root.theme.controlRadius
                    color: previewSaveHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor
                    HoverHandler { id: previewSaveHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.controller.saveMemeImage(memePreviewDialog.imageUrl)
                    }
                    Text {
                        anchors.centerIn: parent
                        text: root.strings.downloadLabel
                        color: "#ffffff"
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.bodySize
                        font.bold: true
                    }
                }
            }
        }
    }
}
