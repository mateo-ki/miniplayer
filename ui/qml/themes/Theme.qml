import QtQuick

QtObject {
    readonly property color windowColor: "#141417"
    readonly property color chromeColor: "#0e0e10"
    readonly property color panelColor: "#1c1c20"
    readonly property color panelRaisedColor: "#26262c"
    readonly property color surfaceColor: "#18181c"
    readonly property color borderColor: "#2a2a30"
    readonly property color subtleBorderColor: "#1a1a1e"
    readonly property color accentColor: "#38bdf8"
    readonly property color accentMutedColor: "#0e263a"
    readonly property color sidebarActiveColor: "#202026"
    readonly property color textPrimaryColor: "#f0f0f5"
    readonly property color textSecondaryColor: "#a0a0ac"
    readonly property color textMutedColor: "#737380"
    readonly property color successColor: "#22c55e"
    readonly property color warningColor: "#f59e0b"
    readonly property color dangerColor: "#ef4444"

    readonly property int windowRadius: 14
    readonly property int panelRadius: 10
    readonly property int controlRadius: 8
    readonly property int edgePadding: 20
    readonly property int gap: 14
    readonly property int sidebarWidth: 316
    readonly property int infoPanelWidth: 340
    readonly property int topBarHeight: 72
    readonly property int controlBarHeight: 88
    readonly property int logPanelHeight: 168

    readonly property int titleSize: 22
    readonly property int sectionTitleSize: 16
    readonly property int bodySize: 13
    readonly property int captionSize: 11
    readonly property string fontFamily: "Segoe UI"
    readonly property string iconFontFamily: {
        var families = Qt.fontFamilies();
        if (families.indexOf("Segoe Fluent Icons") !== -1)
            return "Segoe Fluent Icons";
        if (families.indexOf("Segoe MDL2 Assets") !== -1)
            return "Segoe MDL2 Assets";
        return "Segoe UI Symbol";
    }
}
