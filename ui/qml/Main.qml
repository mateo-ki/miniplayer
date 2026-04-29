import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 1440
    height: 900
    visible: true
    title: "miniPlayer"
    color: "#141414"

    Rectangle {
        anchors.fill: parent
        color: "#141414"

        Text {
            anchors.centerIn: parent
            color: "#f5f5f5"
            text: "miniPlayer bootstrap"
            font.pixelSize: 28
        }
    }
}
