import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"
    property var controller
    property var theme
    property int selectedIndex: root.controller.downloadModel.count > 0 ? 0 : -1
    property int revision: root.controller.downloadModel.revision
    property var selectedEpisodes: selectedIndex >= 0 ? root.controller.downloadModel.episodesForVideo(selectedIndex) : []

    function refreshSelection() {
        if (selectedIndex >= root.controller.downloadModel.count) {
            selectedIndex = root.controller.downloadModel.count > 0 ? 0 : -1
        }
        selectedEpisodes = selectedIndex >= 0 ? root.controller.downloadModel.episodesForVideo(selectedIndex) : []
    }

    onRevisionChanged: {
        refreshSelection()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            radius: theme ? theme.panelRadius : 12
            color: theme ? theme.panelColor : "#1c1c1c"
            border.color: theme ? theme.borderColor : "#353535"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "下载视频"
                        color: theme ? theme.textPrimaryColor : "#f3f3f3"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    BusyIndicator {
                        running: root.controller.downloadModel.active
                        visible: running
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.controller.downloadModel.rootFolder()
                    color: theme ? theme.textMutedColor : "#858585"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }

                ListView {
                    id: videoList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    model: root.controller.downloadModel

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 88
                        radius: theme ? theme.controlRadius : 10
                        color: index === root.selectedIndex
                            ? (theme ? theme.accentMutedColor : "#7a4a17")
                            : (videoMouse.containsMouse ? (theme ? theme.panelRaisedColor : "#242424") : "#151515")
                        border.color: index === root.selectedIndex
                            ? (theme ? theme.accentColor : "#f28c28")
                            : (theme ? theme.subtleBorderColor : "#282828")
                        border.width: 1

                        MouseArea {
                            id: videoMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.selectedIndex = index
                                root.selectedEpisodes = root.controller.downloadModel.episodesForVideo(index)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 52
                                Layout.preferredHeight: 72
                                radius: 6
                                color: theme ? theme.surfaceColor : "#111111"
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: poster || ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: !poster || poster.length === 0
                                    text: "V"
                                    color: theme ? theme.textMutedColor : "#858585"
                                    font.pixelSize: 22
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    Layout.fillWidth: true
                                    text: name
                                    color: theme ? theme.textPrimaryColor : "#f3f3f3"
                                    font.family: theme ? theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 13
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: doneCount + "/" + totalCount + " 集 · " + status
                                    color: theme ? theme.textMutedColor : "#858585"
                                    font.family: theme ? theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }

                                ProgressBar {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    value: progress
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.controller.downloadModel.count === 0
                        text: "还没有下载任务\n可在搜索详情页点“下载本线路”，或播放页点 DL"
                        horizontalAlignment: Text.AlignHCenter
                        color: theme ? theme.textMutedColor : "#858585"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 14
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme ? theme.panelRadius : 12
            color: theme ? theme.panelColor : "#1c1c1c"
            border.color: theme ? theme.borderColor : "#353535"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: root.selectedIndex >= 0 ? "下载集数" : "选择一个视频"
                        color: theme ? theme.textPrimaryColor : "#f3f3f3"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Button {
                        text: "打开文件夹"
                        enabled: root.selectedIndex >= 0
                        onClicked: root.controller.downloadModel.openVideoFolder(root.selectedIndex)
                    }

                    Button {
                        text: "删除视频"
                        enabled: root.selectedIndex >= 0 && !root.controller.downloadModel.active
                        onClicked: {
                            root.controller.downloadModel.deleteVideo(root.selectedIndex, true)
                            root.refreshSelection()
                        }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    clip: true
                    model: root.selectedEpisodes

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 68
                        radius: theme ? theme.controlRadius : 10
                        color: episodeMouse.containsMouse ? (theme ? theme.panelRaisedColor : "#242424") : "#151515"
                        border.color: theme ? theme.subtleBorderColor : "#282828"
                        border.width: 1

                        MouseArea {
                            id: episodeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: theme ? theme.textPrimaryColor : "#f3f3f3"
                                    font.family: theme ? theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 13
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.localPath && modelData.localPath.length > 0 ? modelData.localPath : modelData.url
                                    color: theme ? theme.textMutedColor : "#858585"
                                    font.family: theme ? theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                }
                            }

                            Text {
                                Layout.preferredWidth: 76
                                text: (modelData.m3u8Only ? "m3u8 " : "") + modelData.status + " " + modelData.progress + "%"
                                color: modelData.status === "完成"
                                    ? (theme ? theme.successColor : "#6ec16e")
                                    : (theme ? theme.accentColor : "#f28c28")
                                font.family: theme ? theme.fontFamily : "Segoe UI"
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                            }

                            Button {
                                Layout.preferredWidth: 54
                                text: "重试"
                                enabled: !root.controller.downloadModel.active && modelData.status !== "完成"
                                onClicked: {
                                    root.controller.downloadModel.retryEpisode(root.selectedIndex, index)
                                    root.refreshSelection()
                                }
                            }

                            Button {
                                Layout.preferredWidth: 54
                                text: "删除"
                                enabled: !root.controller.downloadModel.active
                                onClicked: {
                                    root.controller.downloadModel.deleteEpisode(root.selectedIndex, index, true)
                                    root.refreshSelection()
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.selectedEpisodes.length === 0
                        text: "该视频还没有集数"
                        color: theme ? theme.textMutedColor : "#858585"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
}
