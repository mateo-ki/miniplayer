import QtQuick

// B 站风格浮动弹幕引擎。
// 设计为 videoGestureWindow 的子项(z 低于其 MouseArea),画在视频上方但不抢手势。
// 由 Main.qml 绑定:danmaku(模型)、positionMs、isPlaying、seeking、displayMode、fontScale、danmakuOpacity。
Item {
    id: root

    // ── 输入属性 ──────────────────────────────────────
    property bool danmakuEnabled: true              // 总开关
    property var danmaku: []                        // [{id,text,color,timePoint,mode,size}]
    property int positionMs: -1                     // 播放位置(毫秒)
    property bool isPlaying: false
    property bool seeking: false
    property real danmakuOpacity: 0.85              // 0..1 整体不透明度
    property int displayMode: 0                     // 0全部 1滚动 2顶部 3底部
    property real fontScale: 1.0                     // 字号缩放
    property int maxConcurrent: 80                  // 同屏上限(限流)
    property real scrollSpeed: 150.0                // 滚动速度 px/s

    // ── 内部状态 ──────────────────────────────────────
    property int _cursorMs: -1                      // 已 spawn 到的播放时间
    readonly property int _laneHeight: 30            // 单行高度(含间距)
    property var _scrollLanes: []                    // 每行“可再入时间(ms)”
    property var _topLanes: []                       // 顶部固定行占用截止时间
    property var _bottomLanes: []
    property int _nextUid: 1                         // 内部唯一标识

    opacity: danmakuOpacity
    visible: danmakuEnabled && positionMs >= 0
    clip: true

    ListModel { id: activeModel }

    Repeater {
        id: activeRepeater
        model: activeModel

        Item {
            id: dmItem
            property int uid: model.uid
            property int mode: model.mode            // 0=滚动 1=顶部 2=底部
            property color textColor: model.textColor
            property int fontSize: model.fontSize
            property int travelMs: model.travelMs
            property int lifeMs: model.lifeMs
            property int lane: model.lane
            property var activeAnim: null            // 当前驱动动画引用(供暂停/恢复)
            width: dmText.implicitWidth
            height: dmText.implicitHeight
            opacity: mode === 0 ? 1.0 : 0.0
            x: mode === 0 ? root.width : (root.width - width) / 2
            y: mode === 2 ? root.height - (lane + 1) * root._laneHeight - height
                           : lane * root._laneHeight

            Text {
                id: dmText
                text: model.text
                color: dmItem.textColor
                font.pixelSize: dmItem.fontSize
                font.family: "Segoe UI"
                style: Text.Outline
                styleColor: Qt.rgba(0, 0, 0, 0.65)
                renderType: Text.NativeRendering
            }

            // 滚动:右->左匀速。
            NumberAnimation on x {
                id: scrollAnim
                from: root.width
                to: -dmItem.width - 40
                duration: dmItem.travelMs
                easing.type: Easing.Linear
                running: false
                loops: 1
                onStopped: root._retire(dmItem.uid)
            }

            // 顶部/底部:淡入->停留->淡出。
            SequentialAnimation on opacity {
                id: fadeAnim
                running: false
                loops: 1
                NumberAnimation { from: 0.0; to: 1.0; duration: 250 }
                PauseAnimation { duration: Math.max(0, dmItem.lifeMs - 250 - 550) }
                NumberAnimation { from: 1.0; to: 0.0; duration: 550 }
                onStopped: root._retire(dmItem.uid)
            }

            Component.onCompleted: {
                if (mode === 0) {
                    dmItem.activeAnim = scrollAnim
                    scrollAnim.start()
                } else {
                    dmItem.activeAnim = fadeAnim
                    fadeAnim.start()
                }
                if (!root.isPlaying && dmItem.activeAnim)
                    dmItem.activeAnim.pause()
            }
        }
    }

    // ── spawn tick:把 timePoint 落在 (cursor, positionMs] 的弹幕投屏 ──
    Timer {
        id: spawnTimer
        interval: 100
        repeat: true
        running: root.danmakuEnabled && root.positionMs >= 0 && !root.seeking
        onTriggered: root._tick()
    }

    Connections {
        target: root
        function onIsPlayingChanged() { root._syncAnimationPause() }
        function onDanmakuEnabledChanged() {
            if (!root.danmakuEnabled) {
                activeModel.clear()
                root._cursorMs = -1
                root._resetLanes()
            } else {
                root._cursorMs = root.positionMs
            }
            root._syncAnimationPause()
        }
        function onDanmakuChanged() {
            // 新集加载会让 danmaku 先清空;此时清屏并重置游标,避免旧弹幕残留/重放。
            if (!root.danmaku || root.danmaku.length === 0) {
                activeModel.clear()
                root._resetLanes()
            }
        }
        function onPositionMsChanged() {
            if (root.positionMs < 0) return
            // 回跳 -> seek 到过去,清空重对。
            if (root._cursorMs >= 0 && root.positionMs < root._cursorMs - 1000) {
                activeModel.clear()
                root._resetLanes()
            }
            // 前跳过大(>60s) -> 清空重对,避免积压一次性喷出。
            if (root._cursorMs >= 0 && root.positionMs > root._cursorMs + 60000) {
                activeModel.clear()
                root._resetLanes()
            }
        }
        function onSeekingChanged() { root._syncAnimationPause() }
    }

    // ── 引擎逻辑 ──────────────────────────────────────
    function _resetLanes() {
        var laneCount = Math.max(1, Math.floor(height / _laneHeight))
        root._scrollLanes = new Array(laneCount).fill(0)
        root._topLanes = new Array(laneCount).fill(0)
        root._bottomLanes = new Array(laneCount).fill(0)
        root._cursorMs = positionMs
    }

    function _tick() {
        if (positionMs < 0) return
        if (_cursorMs < 0 || _scrollLanes.length === 0) _resetLanes()
        var from = _cursorMs
        var to = positionMs
        if (to < from) { _cursorMs = to; return }
        if (!danmaku || danmaku.length === 0) { _cursorMs = to; return }

        for (var i = 0; i < danmaku.length; i++) {
            var d = danmaku[i]
            var tp = d.timePoint
            if (tp > from && tp <= to)
                _spawn(d)
        }
        _cursorMs = to
    }

    function _spawn(d) {
        if (activeModel.count >= maxConcurrent) return  // 限流

        var mode = d.mode
        // displayMode 过滤:1滚动 2顶部 3底部;0 放全部(按每条自身 mode)。
        if (displayMode === 1 && mode !== 0) return
        if (displayMode === 2 && mode !== 1) return
        if (displayMode === 3 && mode !== 2) return
        var renderMode = mode === 1 ? 1 : (mode === 2 ? 2 : 0)

        var base = d.size === "S" ? 18 : (d.size === "L" ? 30 : 24)
        var fontSize = Math.round(base * fontScale)
        var color = _argb(d.color)

        var lane = -1
        var lifeMs = 0
        var travelMs = 0
        if (renderMode === 0) {
            lane = _allocScroll(positionMs)
            if (lane < 0) return
            var textW = _measureW(d.text, fontSize)
            var travelPx = width + textW + 40
            travelMs = Math.round(travelPx / scrollSpeed * 1000)
            _scrollLanes[lane] = positionMs + Math.round((textW + 20) / scrollSpeed * 1000)
        } else if (renderMode === 1) {
            lane = _allocFixed(_topLanes, positionMs)
            if (lane < 0) return
            lifeMs = 4500
            _topLanes[lane] = positionMs + lifeMs
        } else {
            lane = _allocFixed(_bottomLanes, positionMs)
            if (lane < 0) return
            lifeMs = 4500
            _bottomLanes[lane] = positionMs + lifeMs
        }

        activeModel.append({
            uid: _nextUid++,
            text: d.text,
            mode: renderMode,
            textColor: color,
            fontSize: fontSize,
            travelMs: travelMs,
            lifeMs: lifeMs,
            lane: lane
        })
    }

    function _allocScroll(now) {
        for (var i = 0; i < _scrollLanes.length; i++)
            if (_scrollLanes[i] <= now) return i
        return -1
    }
    function _allocFixed(arr, now) {
        for (var i = 0; i < arr.length; i++)
            if (arr[i] <= now) return i
        return -1
    }

    function _measureW(text, px) {
        _measureText.text = text
        _measureText.font.pixelSize = px
        return _measureText.implicitWidth
    }
    Text {
        id: _measureText
        visible: false
        font.family: "Segoe UI"
        renderType: Text.NativeRendering
    }

    function _argb(c) {
        if (c === undefined || c === null) c = 16777215
        c = Number(c)
        if (isNaN(c) || c <= 0) c = 16777215
        var a = (c >> 24) & 0xff
        var r = (c >> 16) & 0xff
        var g = (c >> 8) & 0xff
        var b = c & 0xff
        if (a === 0) a = 255
        return Qt.rgba(r / 255, g / 255, b / 255, a / 255)
    }

    function _retire(uid) {
        for (var i = 0; i < activeModel.count; i++) {
            if (activeModel.get(i).uid === uid) {
                activeModel.remove(i)
                return
            }
        }
    }

    function _syncAnimationPause() {
        for (var i = 0; i < activeRepeater.count; i++) {
            var item = activeRepeater.itemAt(i)
            if (!item || !item.activeAnim) continue
            if (root.danmakuEnabled && root.isPlaying && !root.seeking)
                item.activeAnim.resume()
            else
                item.activeAnim.pause()
        }
    }

    Component.onCompleted: _resetLanes()
    onHeightChanged: _resetLanes()
    onWidthChanged: _resetLanes()
}
