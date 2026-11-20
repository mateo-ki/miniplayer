import QtQuick
import QtQuick.Layouts

// 「我的」页：pool 后端的动漫收藏与观看历史，外加群聊/私信。
// 控件沿用本仓库自绘 Rectangle+Text+MouseArea 的写法，不依赖 Controls 主题。
Rectangle {
    id: root
    color: theme ? theme.surfaceColor : "#141517"

    property QtObject theme
    property QtObject poolMe
    property QtObject poolChat
    // 页面栈停不停在「我的」这一页由 Main.qml 告知；它只是 syncPolling 的一个输入。
    property bool pageShown: true
    property int tab: 0
    // 聊天标签内部再分两层：chatMode 选群聊/私信，dmPeerId 非空就是在看某一条私信。
    property int chatMode: 0
    property string dmPeerId: ""
    property string dmPeerName: ""
    // 删除要两段确认（就地改字，不插新块）；记下待确认的那条 id。
    property string pendingDeleteId: ""
    // 点头像弹出来的"要不要私聊 TA"：记的是那条消息的 id，再点一次收起。
    property string dmPromptFor: ""
    property string dmPromptUserId: ""
    property string dmPromptName: ""

    function toggleDmPrompt(messageId, userId, name) {
        const key = String(messageId || "")
        if (key.length === 0)
            return
        if (dmPromptFor === key) {
            dmPromptFor = ""
            return
        }
        dmPromptFor = key
        dmPromptUserId = String(userId || "")
        dmPromptName = String(name || "匿名")
    }

    function confirmDmPrompt() {
        const userId = dmPromptUserId
        const name = dmPromptName
        dmPromptFor = ""
        dmPromptUserId = ""
        dmPromptName = ""
        openDm(userId, name)
    }
    // 看大图只有一个入口：记下这张图的地址，覆盖层负责显示与关闭。
    property string previewUrl: ""
    // 输入里当前停在末尾的 ##关键词##（空就是不弹候选）。
    property string animeKeyword: ""
    readonly property string pendingAnimeText: poolChat ? poolChat.pendingAnimeName : ""

    function showImage(url) {
        const value = String(url || "")
        if (value.length > 0)
            previewUrl = value
    }
    // @ 补全的关键字与浮层可见性。
    property string mentionKeyword: ""
    // tab 是人在标签条上数的第几格，pane 是 StackLayout 里那块的真实次序；
    // 聊天挪到第一格只动这张对照表，下面所有逻辑仍然按 pane 判（0=收藏 1=历史 2=聊天）。
    readonly property var paneOfTab: [2, 0, 1]
    readonly property int pane: paneOfTab[tab]
    readonly property var tabNames: ["聊天", "收藏", "观看历史"]
    readonly property var chatModeNames: ["群聊", "私信"]
    readonly property bool dmOpen: dmPeerId.length > 0
    readonly property string retentionHint: "群聊只保留 7 天，私信只保留 1 天，过期由服务器自动清掉"
    readonly property string statusMessage: pane === 2 ? chatStatusMessage
        : (pane === 0 ? (poolMe ? poolMe.favoritesMessage : "")
                      : (poolMe ? poolMe.historyMessage : ""))
    readonly property string chatStatusMessage: !poolChat ? ""
        : (chatMode === 0 ? poolChat.message : (dmOpen ? poolChat.threadMessage : poolChat.threadsMessage))
    readonly property bool busy: pane === 2 ? chatBusy
        : (poolMe ? (poolMe.favoritesLoading || poolMe.historyLoading) : false)
    readonly property bool chatBusy: !poolChat ? false
        : (chatMode === 0 ? poolChat.groupLoading : (dmOpen ? poolChat.threadLoading : poolChat.threadsLoading))
    // 输入框按 UTF-16 码元截断，永远不会超过服务端的 500 字（标量）限额，只会更早。
    readonly property int draftLimit: poolChat ? poolChat.maxContentLength : 500
    readonly property var mentionCandidates: candidatesFor(mentionKeyword)

    // 收藏/历史带回来的是上游编号，新版是 18 位数字，装不进 int，全程按字符串传。
    signal openRequested(string vodKey, string name)

    // 进页面就重取一次：启动时那趟 reload 撞在登录完成前，会把"请先登录"留在标签上。
    // visible 与 pageShown 任一翻真都算进页，判定只走 refreshOnEntry 这一个入口。
    function refreshOnEntry() {
        if (visible && pageShown)
            reload()
    }
    onVisibleChanged: {
        syncPolling()
        refreshOnEntry()
    }
    onTabChanged: {
        pendingDeleteId = ""
        mentionKeyword = ""
        previewUrl = ""
        dmPromptFor = ""
        groupEmoji.open = false
        dmEmoji.open = false
        syncPolling()
    }
    onChatModeChanged: {
        pendingDeleteId = ""
        mentionKeyword = ""
        groupEmoji.open = false
        dmEmoji.open = false
        syncPolling()
    }
    onDmPeerIdChanged: {
        pendingDeleteId = ""
        syncPolling()
    }
    onPageShownChanged: {
        syncPolling()
        refreshOnEntry()
    }

    // 取数只有一个入口：进页、切标签、点刷新都走它。
    function reload() {
        if (pane === 2) {
            if (!poolChat)
                return
            if (chatMode === 0)
                poolChat.refreshGroup()
            else if (dmOpen)
                poolChat.openThread(dmPeerId, dmPeerName)
            else
                poolChat.refreshThreads()
            return
        }
        if (!poolMe)
            return
        if (pane === 0)
            poolMe.refreshFavorites()
        else
            poolMe.refreshHistory()
    }

    // 头像只取名字第一个字（中文名字整体就是一个字一个词，截 1 个码元够用）。
    function initialOf(name) {
        const value = String(name || "").trim()
        return value.length > 0 ? value.substring(0, 1) : "?"
    }

    // 贴底只有一个开关：落到这一屏时开，人往上翻关，翻回底部再开。
    property bool stickGroup: false
    property bool stickDm: false

    // 三个时机都会喂这一个函数：内容长高、条数变化、视图本身刚拿到真实高度。
    // 之前只在落底那一刻滚一次，赶上 ListView 高度还是 0 就成了空操作，人看到的还是最上面。
    function holdBottom(list, stick) {
        if (!list || !stick || list.atYEnd)
            return
        Qt.callLater(function () { list.positionViewAtEnd() })
    }

    // 落底按列表各自触发（谁的可见性变了谁开自己的开关），
    // 免得群聊的 onVisibleChanged 去动私信的开关。
    // Ctrl+V 发图：整个窗口都能接，不用先把光标点进输入框；
    // 只有剪贴板确实有图时它才存在，所以不会抢走正常的文本粘贴。
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root.pane === 2 && root.poolChat && root.poolChat.clipboardHasImage
        onActivated: root.poolChat.pasteImageFromClipboard()
    }

    function landGroup() {
        stickGroup = true
        holdBottom(groupList, true)
    }

    function landDm() {
        stickDm = true
        holdBottom(dmList, true)
    }

    // 轮询只有一个通道：本页可见 + 页面栈停在这页 + 停在聊天标签，三个条件都在
    // syncPolling 里现算，切标签、离开「我的」都会关掉定时器；定时器喂哪个视图也在这里现算
    // （群聊/会话列表/单个对话三选一，不会同时拉三条接口）。进聊天时顺手取一次 @ 候选名单。
    // 可见性、标签、页面栈索引、chatMode、dmPeerId 都只喂给这一个入口，别在别处再开/关轮询。
    function syncPolling() {
        if (!poolChat)
            return
        const showChat = visible && pageShown && pane === 2
        poolChat.setActive(showChat)
        if (showChat) {
            poolChat.setViewMode(chatMode === 0 ? 0 : (dmOpen ? 2 : 1))
            poolChat.searchUsers("")
        }
    }

    function isMine(item) {
        return !!(item && item.mine)
    }

    function confirmDelete(id, isDirect) {
        const key = String(id || "")
        if (key.length === 0)
            return
        if (pendingDeleteId !== key) {
            pendingDeleteId = key
            return
        }
        pendingDeleteId = ""
        if (!poolChat)
            return
        if (isDirect)
            poolChat.deleteThreadMessage(key)
        else
            poolChat.deleteGroupMessage(key)
    }

    // 发送只认控制器那一个判定：先问一次能不能发（同一个函数、同一个时钟），
    // 放行才清草稿；被挡就把草稿留着，原因由控制器的状态行给出。
    function sendDraft(text, isDirect) {
        const draft = String(text || "").trim()
        if (!poolChat)
            return
        const blocked = isDirect ? poolChat.threadBlockReason(draft)
                                 : poolChat.groupBlockReason(draft)
        if (blocked.length === 0) {
            if (isDirect)
                dmInput.text = ""
            else
                groupInput.text = ""
        }
        if (isDirect)
            poolChat.sendThreadMessage(draft)
        else
            poolChat.sendGroupMessage(draft)
    }

    // 开私信只有一个入口：群聊里右键头像、私信列表里点会话，都走这里。
    function openDm(peerId, peerName) {
        const id = String(peerId || "")
        if (!poolChat || id.length === 0)
            return
        chatMode = 1
        dmPeerId = id
        dmPeerName = String(peerName || "")
        poolChat.openThread(dmPeerId, dmPeerName)
    }

    function closeDm() {
        if (poolChat)
            poolChat.closeThread()
        dmPeerId = ""
        dmPeerName = ""
    }

    function pointCount(text) {
        // 跟服务端 chars().count() 对齐：代理对（emoji）在 JS 字符串里占 2 个元素，只数一次。
        const value = String(text || "")
        let count = 0
        for (let i = 0; i < value.length; ++i) {
            const code = value.charCodeAt(i)
            if (!(code >= 0xDC00 && code <= 0xDFFF))
                ++count
        }
        return count
    }

    function timeOf(value) {
        const raw = String(value || "")
        // created_at 是服务端 UTC（RFC3339）；这里按本地时区显示，和网页版看到的时刻一致。
        const parts = /^(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2})/.exec(raw)
        if (!parts)
            return raw
        const when = new Date(Date.UTC(Number(parts[1]), Number(parts[2]) - 1,
                                       Number(parts[3]), Number(parts[4]), Number(parts[5])))
        const now = new Date()
        const hh = ("0" + when.getHours()).slice(-2)
        const mm = ("0" + when.getMinutes()).slice(-2)
        const sameDay = when.getFullYear() === now.getFullYear()
            && when.getMonth() === now.getMonth()
            && when.getDate() === now.getDate()
        if (sameDay)
            return hh + ":" + mm
        return (when.getMonth() + 1) + "/" + when.getDate() + " " + hh + ":" + mm
    }

    // 光标停在 "@xxx" 段里时把这段当作关键字；否则不弹候选。
    function mentionQuery(text) {
        const tail = /(^|\s)@([^\s@]*)$/.exec(String(text || ""))
        return tail ? String(tail[2]) : ""
    }

    function candidatesFor(keyword) {
        const kw = String(keyword || "")
        if (kw.length === 0 || !poolChat)
            return []
        const out = []
        const all = poolChat.users || []
        for (let i = 0; i < all.length && out.length < 6; ++i) {
            const name = String(all[i].username || "")
            if (name.length > 0 && name.indexOf(kw) !== -1)
                out.push(name)
        }
        return out
    }

    function trackMention(text) {
        const kw = mentionQuery(text)
        if (kw === mentionKeyword)
            return
        mentionKeyword = kw
    }

    // ##片名## —— 和网页版同一个正则、同一个防抖：停在末尾的那段就是关键词。
    function animeQuery(text) {
        const tail = /##(\S*)$/.exec(String(text || ""))
        if (!tail)
            return ""
        return String(tail[1]).replace(/##$/, "").trim()
    }

    function trackAnime(text) {
        const kw = animeQuery(text)
        if (kw === animeKeyword)
            return
        animeKeyword = kw
        if (kw.length === 0)
            return
        animeSearchTimer.restart()
    }

    // 选中一条：卡片挂到控制器，输入框里那段 ##…## 抹掉（网页版 choose 也是这么做的）。
    function pickAnimeAt(index, field) {
        const results = poolChat ? poolChat.animeResults : []
        if (index < 0 || index >= results.length)
            return
        const item = results[index]
        poolChat.pickAnime(String(item.name || ""), String(item.pic || ""), String(item.id || ""))
        animeKeyword = ""
        if (field)
            field.text = String(field.text).replace(/##\S*$/, "").trim()
    }

    Timer {
        id: animeSearchTimer
        interval: 350
        repeat: false
        onTriggered: {
            if (poolChat)
                poolChat.searchAnime(root.animeKeyword)
        }
    }

    function pickMention(name) {
        mentionKeyword = ""
        groupInput.text = String(groupInput.text).replace(/(^|\s)@([^\s@]*)$/, "$1@" + name + " ")
        groupInput.cursorPosition = groupInput.text.length
    }

    function favoriteKey(item) {
        const key = String(item.fav_key || item.id || "")
        return key
    }

    // 服务端原样存客户端给的键，新旧协议都可能是 18 位数字，别在这里 parseInt。
    function vodKeyOf(item) {
        return String(item.video_id || item.fav_key || item.id || "")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 16
        anchors.bottomMargin: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: root.tabNames
                Rectangle {
                    Layout.preferredWidth: tabLabel.implicitWidth + 22
                    Layout.preferredHeight: 30
                    radius: root.theme ? root.theme.controlRadius : 8
                    color: root.tab === index
                        ? (root.theme ? root.theme.accentMutedColor : "#14382c")
                        : (root.theme ? root.theme.panelColor : "#1f2023")
                    border.color: root.tab === index
                        ? (root.theme ? root.theme.accentColor : "#10b981")
                        : (root.theme ? root.theme.subtleBorderColor : "#282828")
                    border.width: 1

                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: modelData
                        color: root.tab === index
                            ? (root.theme ? root.theme.accentColor : "#10b981")
                            : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 13
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.tab === index)
                                return
                            root.tab = index
                            root.reload()
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: root.busy
                text: "加载中…"
                color: root.theme ? root.theme.textMutedColor : "#858585"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: 12
            }

            Rectangle {
                Layout.preferredWidth: refreshLabel.implicitWidth + 22
                Layout.preferredHeight: 30
                radius: root.theme ? root.theme.controlRadius : 8
                color: refreshMouse.containsMouse
                    ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                    : (root.theme ? root.theme.panelColor : "#1f2023")
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1

                Text {
                    id: refreshLabel
                    anchors.centerIn: parent
                    text: "刷新"
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 13
                }

                MouseArea {
                    id: refreshMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.reload()
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.statusMessage
            color: root.theme ? root.theme.textSecondaryColor : "#e0a33a"
            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
            font.pixelSize: 12
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.pane

            ListView {
                id: favoritesList
                clip: true
                spacing: 8
                model: root.poolMe ? root.poolMe.favorites : []

                Text {
                    anchors.centerIn: parent
                    visible: favoritesList.count === 0 && !root.busy
                    text: "还没有收藏"
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                }

                delegate: Rectangle {
                    width: favoritesList.width
                    height: 52
                    radius: root.theme ? root.theme.controlRadius : 8
                    color: rowMouse.containsMouse
                        ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                        : (root.theme ? root.theme.panelColor : "#1f2023")
                    border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: modelData.name && modelData.name.length > 0
                                ? modelData.name : ("收藏 " + root.favoriteKey(modelData))
                            color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 13
                        }

                        Text {
                            text: "取消"
                            color: cancelMouse.containsMouse
                                ? (root.theme ? root.theme.accentColor : "#10b981")
                                : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 13
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openRequested(root.vodKeyOf(modelData), modelData.name || "")
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 52
                        height: parent.height
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.poolMe.toggleFavorite(root.vodKeyOf(modelData),
                                                             modelData.name || "",
                                                             modelData.pic || "")
                    }
                }
            }

            ListView {
                id: historyList
                clip: true
                spacing: 8
                model: root.poolMe ? root.poolMe.history : []

                Text {
                    anchors.centerIn: parent
                    visible: historyList.count === 0 && !root.busy
                    text: "还没有观看记录"
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                }

                delegate: Rectangle {
                    width: historyList.width
                    height: 52
                    radius: root.theme ? root.theme.controlRadius : 8
                    color: historyRowMouse.containsMouse
                        ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                        : (root.theme ? root.theme.panelColor : "#1f2023")
                    border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                    border.width: 1

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: favButton.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: modelData.name || ("ID " + (modelData.video_id || ""))
                            color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 13
                        }

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: (modelData.part || "未知集数") + " · 看过 " + (modelData.plays || 1) + " 次"
                            color: root.theme ? root.theme.textMutedColor : "#858585"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        id: favButton
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        width: favLabel.implicitWidth + 18
                        height: 28
                        radius: 6
                        color: favMouse.containsMouse
                            ? (root.theme ? root.theme.accentMutedColor : "#14382c")
                            : "transparent"
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1

                        Text {
                            id: favLabel
                            anchors.centerIn: parent
                            text: root.poolMe && root.poolMe.isFavorite(root.vodKeyOf(modelData), modelData.name || "")
                                ? "已收藏" : "收藏"
                            color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: favMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.poolMe.toggleFavorite(root.vodKeyOf(modelData),
                                                                 modelData.name || "", "")
                        }
                    }

                    MouseArea {
                        id: historyRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openRequested(root.vodKeyOf(modelData), modelData.name || "")
                    }
                }
            }

            // ── 聊天：群聊 + 私信 ─────────────────────────────
            Item {
                id: chatPane
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: root.chatModeNames

                            Rectangle {
                                Layout.preferredWidth: chatModeLabel.implicitWidth + 22
                                Layout.preferredHeight: 28
                                radius: root.theme ? root.theme.controlRadius : 8
                                color: root.chatMode === index
                                    ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                                    : (root.theme ? root.theme.panelColor : "#1c1c20")
                                border.color: root.chatMode === index
                                    ? (root.theme ? root.theme.accentColor : "#38bdf8")
                                    : (root.theme ? root.theme.subtleBorderColor : "#1a1a1e")
                                border.width: 1

                                Text {
                                    id: chatModeLabel
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: root.chatMode === index
                                        ? (root.theme ? root.theme.accentColor : "#38bdf8")
                                        : (root.theme ? root.theme.textSecondaryColor : "#a0a0ac")
                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.chatMode === index)
                                            return
                                        root.chatMode = index
                                        root.reload()
                                    }
                                }

                                // 未读红点贴在标签右上角的浮层：不进布局，
                                // 所以它亮起或灭掉那一下不会把别的标签挤得挪位。
                                Rectangle {
                                    readonly property int count: !root.poolChat ? 0
                                        : (index === 0 ? root.poolChat.unreadGroup
                                                        : root.poolChat.unreadDm)
                                    visible: count > 0
                                    width: 8
                                    height: 8
                                    radius: 4
                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    anchors.top: parent.top
                                    anchors.topMargin: 4
                                    color: root.theme ? root.theme.dangerColor : "#ef4444"
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // 返回会话列表：槽位宽度固定，只在私信对话里亮起，点它版面不重排。
                        Rectangle {
                            id: backToThreads
                            readonly property bool usable: root.chatMode === 1 && root.dmOpen
                            Layout.preferredWidth: 84
                            Layout.preferredHeight: 28
                            radius: root.theme ? root.theme.controlRadius : 8
                            opacity: usable ? 1.0 : 0.0
                            color: backMouse.containsMouse && usable
                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                : "transparent"
                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "‹ 会话列表"
                                color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 12
                            }

                            MouseArea {
                                id: backMouse
                                anchors.fill: parent
                                enabled: backToThreads.usable
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.closeDm()
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.retentionHint
                        color: root.theme ? root.theme.textMutedColor : "#737380"
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 11
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: root.chatMode === 0 ? 0 : (root.dmOpen ? 2 : 1)

                        // ── 群聊 ──
                        Item {
                            id: groupRoom
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8

                                ListView {
                                    id: groupList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 6
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: root.poolChat ? root.poolChat.messages : []

                                    Text {
                                        anchors.centerIn: parent
                                        visible: groupList.count === 0 && !root.busy
                                        text: "群里还没有消息"
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    }

                                    // 贴底/跟随只喂 holdBottom 这一个函数，开关只有 landGroup 开、往上翻关。
                                    onCountChanged: root.holdBottom(groupList, root.stickGroup)
                                    onContentHeightChanged: root.holdBottom(groupList, root.stickGroup)
                                    onHeightChanged: root.holdBottom(groupList, root.stickGroup)
                                    onMovementStarted: root.stickGroup = false
                                    onAtYEndChanged: if (atYEnd) root.stickGroup = true
                                    onVisibleChanged: if (visible) root.landGroup()

                                    delegate: Item {
                                        id: groupRow
                                        width: groupList.width
                                        height: groupBubble.height + 12
                                        objectName: "groupRow"
                                        readonly property bool mine: root.isMine(modelData)
                                        readonly property bool unreadEdge: root.poolChat
                                            && String(modelData.id || "") === root.poolChat.groupFirstUnreadId
                                        // 气泡最宽只到行宽的 62%，短消息就缩成一小块，不再铺满整行。
                                        readonly property real bubbleTextMax: Math.max(90, width * 0.62) - 20

                                        // 未读分隔线浮在行顶：插进布局会让人下面整片重新排。
                                        Rectangle {
                                            visible: groupRow.unreadEdge
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            height: 1
                                            opacity: 0.75
                                            color: root.theme ? root.theme.accentColor : "#38bdf8"
                                        }

                                        Text {
                                            visible: groupRow.unreadEdge
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.leftMargin: 10
                                            anchors.topMargin: 2
                                            text: "以下为未读"
                                            color: root.theme ? root.theme.accentColor : "#38bdf8"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 10
                                        }

                                        // 方案 B：删除位跟着自己这一侧走 —— 只在自己那条出现，
                                        // 贴在气泡左边；别人的行不占这一格，行尾也不留空列。
                                        Rectangle {
                                            id: groupDeleteSlot
                                            readonly property bool mine: groupRow.mine
                                            readonly property bool armed: root.pendingDeleteId
                                                === String(modelData.id || "")
                                            visible: mine
                                            width: 48
                                            height: 24
                                            x: groupBubble.x - 8 - width
                                            y: groupBubble.y + 4
                                            radius: 6
                                            color: groupDeleteMouse.containsMouse
                                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                                : "transparent"
                                            border.color: armed
                                                ? (root.theme ? root.theme.dangerColor : "#ef4444")
                                                : (root.theme ? root.theme.subtleBorderColor : "#1a1a1e")
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: groupDeleteSlot.armed ? "确认删" : "删除"
                                                color: groupDeleteSlot.armed
                                                    ? (root.theme ? root.theme.dangerColor : "#ef4444")
                                                    : (root.theme ? root.theme.textSecondaryColor : "#a0a0ac")
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 11
                                            }

                                            MouseArea {
                                                id: groupDeleteMouse
                                                anchors.fill: parent
                                                enabled: groupDeleteSlot.mine
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.confirmDelete(modelData.id, false)
                                            }
                                        }

                                        // 整行不用布局引擎：头像贴自己那一侧的行边，
                                        // 气泡贴头像内侧 8px，两条算式写死，谁也不会被挤开。
                                        Item {
                                            id: groupBubbleRow
                                            objectName: "groupBubbleRow"
                                            anchors.fill: parent

                                            Rectangle {
                                                objectName: "groupAvatar"
                                                id: groupAvatar
                                                width: 26
                                                height: 26
                                                y: 6
                                                x: groupRow.mine ? (groupBubbleRow.width - 10 - width) : 10
                                                radius: 13
                                                color: root.theme ? root.theme.accentMutedColor : "#17324d"
                                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                                border.width: 1

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: root.initialOf(modelData.username)
                                                    color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                }

                                                // 点别人的头像 = 弹一个"私聊 TA"的确认框（左键右键都行）；
                                                // 自己的头像没有这个动作。
                                                MouseArea {
                                                    anchors.fill: parent
                                                    enabled: !groupRow.mine
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.toggleDmPrompt(modelData.id, modelData.user_id,
                                                                                   modelData.username)
                                                }
                                            }

                                            // 贴在头像下方的确认条：浮在行里，不作为布局的一格。
                                            Rectangle {
                                                id: groupDmPrompt
                                                visible: root.dmPromptFor === String(modelData.id || "")
                                                    && !groupRow.mine
                                                x: groupAvatar.x
                                                y: groupAvatar.y + groupAvatar.height + 4
                                                width: groupDmPromptRow.implicitWidth + 16
                                                height: 28
                                                z: 30
                                                radius: 6
                                                color: root.theme ? root.theme.panelRaisedColor : "#26262c"
                                                border.color: root.theme ? root.theme.borderColor : "#2a2a30"
                                                border.width: 1

                                                Row {
                                                    id: groupDmPromptRow
                                                    anchors.centerIn: parent
                                                    spacing: 8

                                                    Text {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: "私聊 " + root.dmPromptName + "？"
                                                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 12
                                                    }

                                                    Text {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: "开"
                                                        color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }

                                                    Text {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: "收"
                                                        color: root.theme ? root.theme.textMutedColor : "#858585"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 12
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        // 右半是"开"，左半（含问题文案）是"收"。
                                                        if (mouse.x > width - 58)
                                                            root.confirmDmPrompt()
                                                        else
                                                            root.dmPromptFor = ""
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                id: groupBubble
                                                objectName: groupRow.mine ? "groupBubbleMine" : "groupBubbleOther"
                                                // 宽 = 内容宽，最宽到行宽 62%；高 = 内容高。
                                                width: Math.min(groupBubbleCol.implicitWidth + 18,
                                                                Math.max(140, groupBubbleRow.width * 0.62))
                                                height: groupBubbleCol.implicitHeight + 14
                                                y: 6
                                                // 自己的：气泡右边缘 = 头像左边缘 - 8；别人的反过来。
                                                x: groupRow.mine ? (groupAvatar.x - 8 - width) : (groupAvatar.x + groupAvatar.width + 8)
                                                radius: 9
                                                color: groupRow.mine
                                                    ? (root.theme ? root.theme.accentMutedColor : "#14382c")
                                                    : (root.theme ? root.theme.panelColor : "#1c1c20")
                                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                                border.width: 1

                                                ColumnLayout {
                                                    id: groupBubbleCol
                                                    anchors.left: parent.left
                                                    anchors.top: parent.top
                                                    anchors.margins: 9
                                                    spacing: 2

                                                    RowLayout {
                                                        spacing: 6
                                                        layoutDirection: Qt.LeftToRight

                                                        Text {
                                                            text: modelData.username || "匿名"
                                                            color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                            font.pixelSize: 12
                                                            font.bold: true
                                                        }

                                                        Text {
                                                            text: root.timeOf(modelData.created_at)
                                                            color: root.theme ? root.theme.textMutedColor : "#737380"
                                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                            font.pixelSize: 11
                                                        }
                                                    }

                                                    Text {
                                                        // 宽随内容收缩，撞到上限才换行；气泡就这么大。
                                                        Layout.preferredWidth: Math.min(implicitWidth, groupRow.bubbleTextMax)
                                                        Layout.maximumWidth: groupRow.bubbleTextMax
                                                        visible: String(modelData.content || "").length > 0
                                                        text: modelData.content || ""
                                                        wrapMode: Text.Wrap
                                                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 13
                                                    }

                                                    // 收到的动漫卡片：封面 + 片名，和网页版一样是一张卡而不是一行字。
                                                    // 整张卡可点：走 openRequested 那条已经验过的链路
                                                    // （清 activePageOverride -> 切到动漫页 -> 按编号开详情）。
                                                    // 外面套一层 Item，是为了让鼠标层盖住整张卡，
                                                    // 又不会作为额外一格挤进气泡的 ColumnLayout。
                                                    Item {
                                                        id: groupAnimeCard
                                                        visible: !!(modelData.anime && modelData.anime.name)
                                                        // 卡片带的是上游 18 位编号，正好是 loadDetailByKey 吃的键。
                                                        readonly property string key: modelData.anime
                                                            ? String(modelData.anime.id || "") : ""
                                                        implicitWidth: groupAnimeCardRow.implicitWidth
                                                        implicitHeight: groupAnimeCardRow.implicitHeight
                                                        Layout.preferredWidth: implicitWidth
                                                        Layout.preferredHeight: implicitHeight

                                                        RowLayout {
                                                            id: groupAnimeCardRow
                                                            anchors.fill: parent
                                                            spacing: 6

                                                        Rectangle {
                                                            // 海报尺寸：原来 30x38 小得连片名都看不清
                                                            Layout.preferredWidth: 140
                                                            Layout.preferredHeight: 190
                                                            Layout.alignment: Qt.AlignTop
                                                            radius: 3
                                                            color: root.theme ? root.theme.panelRaisedColor : "#26262c"

                                                            Image {
                                                                id: cardCover
                                                                anchors.fill: parent
                                                                anchors.margins: 1
                                                                source: modelData.anime ? String(modelData.anime.pic || "") : ""
                                                                asynchronous: true
                                                                fillMode: Image.PreserveAspectCrop
                                                                visible: status === Image.Ready
                                                            }

                                                            Text {
                                                                anchors.centerIn: parent
                                                                visible: !cardCover.visible
                                                                text: cardCover.status === Image.Error ? "无图" : "…"
                                                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                                                font.pixelSize: 9
                                                            }
                                                        }

                                                        Text {
                                                            Layout.preferredWidth: Math.min(implicitWidth, Math.max(90, groupRow.bubbleTextMax - 152))
                                                            Layout.maximumWidth: Math.max(90, groupRow.bubbleTextMax - 152)
                                                        text: "《" + (modelData.anime ? modelData.anime.name : "") + "》"
                                                        elide: Text.ElideRight
                                                        color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 17
                                                    }
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            enabled: groupAnimeCard.key.length > 0
                                                            hoverEnabled: true
                                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                            onClicked: root.openRequested(groupAnimeCard.key,
                                                                modelData.anime ? String(modelData.anime.name || "") : "")
                                                        }
                                                    }

                                                    ChatThumbs {
                                                        visible: (modelData.images ? modelData.images.length : 0) > 0
                                                        Layout.preferredWidth: Math.min(implicitWidth, groupRow.bubbleTextMax)
                                                        Layout.maximumWidth: groupRow.bubbleTextMax
                                                        images: modelData.images
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // 待发送的图（已上传拿到直链的）：这一条只在挂了图/在传时占高，
                                // 摘干净就还给消息列表，不会长期挤掉一块版面。
                                Item {
                                    Layout.fillWidth: true
                                    readonly property bool showing: (poolChat ? poolChat.pendingImages.length : 0) > 0
                                        || (poolChat ? poolChat.imageUploading : false)
                                    visible: showing
                                    implicitHeight: showing ? pendingRow.implicitHeight + 6 : 0
                                    Layout.preferredHeight: implicitHeight

                                    Row {
                                        id: pendingRow
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        spacing: 6

                                        Repeater {
                                            model: root.poolChat ? root.poolChat.pendingImages : []

                                            Rectangle {
                                                id: pendingImage
                                                required property string modelData
                                                required property int index
                                                width: 76
                                                height: 76
                                                radius: 6
                                                objectName: "pendingImage"
                                                color: root.theme ? root.theme.panelRaisedColor : "#26262c"
                                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                                border.width: 1

                                                Image {
                                                    id: pendingCover
                                                    anchors.fill: parent
                                                    anchors.margins: 2
                                                    source: pendingImage.modelData
                                                    asynchronous: true
                                                    fillMode: Image.PreserveAspectCrop
                                                    visible: status === Image.Ready
                                                }

                                                Text {
                                                    anchors.centerIn: parent
                                                    visible: !pendingCover.visible
                                                    text: pendingCover.status === Image.Error ? "无图" : "…"
                                                    color: root.theme ? root.theme.textMutedColor : "#737380"
                                                    font.pixelSize: 10
                                                }

                                                // 摘掉这张：点整块就行，不用再瞄一个小 ✕。
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.poolChat.removePendingImage(pendingImage.index)
                                                }
                                            }
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: root.poolChat && root.poolChat.imageMessage.length > 0
                                            text: root.poolChat ? root.poolChat.imageMessage : ""
                                            color: root.theme ? root.theme.textMutedColor : "#858585"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                Rectangle {
                                    id: groupInputRow
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    radius: root.theme ? root.theme.controlRadius : 8
                                    color: root.theme ? root.theme.panelColor : "#1c1c20"
                                    border.color: groupInput.activeFocus
                                        ? (root.theme ? root.theme.accentColor : "#38bdf8")
                                        : (root.theme ? root.theme.subtleBorderColor : "#1a1a1e")
                                    border.width: 1

                                    AnimePicker { field: groupInput }
                                    EmojiPicker { id: groupEmoji; field: groupInput }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 6
                                        spacing: 8

                                        // 挂了待发送的动漫卡片就在这里亮一颗胶囊，点 ✕ 摘掉。
                                        Rectangle {
                                            visible: root.pendingAnimeText.length > 0
                                            Layout.preferredHeight: 24
                                            radius: 12
                                            color: root.theme ? root.theme.accentMutedColor : "#0e263a"
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1

                                            Row {
                                                anchors.centerIn: parent
                                                anchors.margins: 8
                                                spacing: 6

                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "《" + root.pendingAnimeText + "》"
                                                    color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                    font.pixelSize: 11
                                                }

                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "✕"
                                                    color: root.theme ? root.theme.textMutedColor : "#737380"
                                                    font.pixelSize: 11
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: if (root.poolChat)
                                                    root.poolChat.clearAnime()
                                            }
                                        }

                                        TextInput {
                                            id: groupInput
                                            Layout.fillWidth: true
                                            color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 13
                                            selectByMouse: true
                                            clip: true
                                            verticalAlignment: TextInput.AlignVCenter
                                            maximumLength: root.draftLimit
                                            onTextChanged: {
                                                root.trackMention(text)
                                                root.trackAnime(text)
                                            }
                                            // 焦点在输入框里时，Ctrl+V 会被 TextInput 自己吃掉，
                                            // 窗口级 Shortcut 收不到 —— 必须在这里先拦一道。
                                            // 焦点在输入框里时，Ctrl+V 会被 TextInput 自己吃掉，
                                            // 窗口级 Shortcut 收不到 —— 必须在这里先拦一道。
                                            Keys.onPressed: (event) => {
                                                if (event.key === Qt.Key_V
                                                        && (event.modifiers & Qt.ControlModifier)
                                                        && root.poolChat
                                                        && root.poolChat.clipboardHasImage) {
                                                    root.poolChat.pasteImageFromClipboard()
                                                    event.accepted = true
                                                }
                                            }
                                            onAccepted: root.sendDraft(text, false)

                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                visible: !groupInput.text && !groupInput.activeFocus
                                                text: "说点什么，@ 可以直接打用户名"
                                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 13
                                            }
                                        }

                                        Text {
                                            // 字数槽位定宽，字数变了也不挤动输入框。
                                            Layout.preferredWidth: 56
                                            horizontalAlignment: Text.AlignRight
                                            text: root.pointCount(groupInput.text) + "/" + root.draftLimit
                                            color: root.pointCount(groupInput.text) > root.draftLimit
                                                ? (root.theme ? root.theme.warningColor : "#f59e0b")
                                                : (root.theme ? root.theme.textMutedColor : "#737380")
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 11
                                        }

                                        // 表情：点开浮在输入行上方的网格，选中就插到光标处；面板可以连着点几个。
                                        Rectangle {
                                            Layout.preferredWidth: 44
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: groupEmojiMouse.containsMouse
                                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                                : (groupEmoji.open
                                                    ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                                                    : "transparent")
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: "表情"
                                                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 12
                                            }

                                            MouseArea {
                                                id: groupEmojiMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: groupEmoji.open = !groupEmoji.open
                                            }
                                        }

                                        // 选图 → 逐张上传拿直链，挂在待发送里。
                                        Rectangle {
                                            Layout.preferredWidth: 40
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: groupInputPickMouse.containsMouse
                                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                                : "transparent"
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1
                                            enabled: !(root.poolChat && root.poolChat.imageUploading)

                                            Text {
                                                anchors.centerIn: parent
                                                text: root.poolChat && root.poolChat.imageUploading ? "传中" : "图"
                                                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 12
                                            }

                                            MouseArea {
                                                id: groupInputPickMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: if (root.poolChat) root.poolChat.chooseAndUploadImages()
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 52
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: groupSendMouse.containsMouse
                                                ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                                                : "transparent"
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: "发送"
                                                color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 12
                                            }

                                            MouseArea {
                                                id: groupSendMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.sendDraft(groupInput.text, false)
                                            }
                                        }
                                    }

                                    // @ 候选浮层：贴在输入行上沿，不插进布局，所以点它不会重排。
                                    Rectangle {
                                        id: mentionPopup
                                        visible: root.chatMode === 0
                                            && groupInput.activeFocus
                                            && root.mentionCandidates.length > 0
                                        width: parent.width
                                        height: mentionColumn.implicitHeight + 12
                                        anchors.left: parent.left
                                        anchors.bottom: parent.top
                                        z: 20
                                        radius: 6
                                        color: root.theme ? root.theme.panelRaisedColor : "#26262c"
                                        border.color: root.theme ? root.theme.borderColor : "#2a2a30"
                                        border.width: 1

                                        Column {
                                            id: mentionColumn
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 6
                                            spacing: 2

                                            Repeater {
                                                model: root.mentionCandidates

                                                Rectangle {
                                                    width: mentionColumn.width
                                                    height: 22
                                                    radius: 4
                                                    color: mentionMouse.containsMouse
                                                        ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                                                        : "transparent"

                                                    Text {
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 6
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: "@ " + modelData
                                                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 12
                                                    }

                                                    MouseArea {
                                                        id: mentionMouse
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.pickMention(modelData)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ── 私信：会话列表 ──
                        ListView {
                            id: threadList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            boundsBehavior: Flickable.StopAtBounds
                            model: root.poolChat ? root.poolChat.threads : []

                            Text {
                                anchors.centerIn: parent
                                visible: threadList.count === 0 && !root.busy
                                text: "还没有私信"
                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            }

                            delegate: Rectangle {
                                width: threadList.width
                                height: 54
                                radius: root.theme ? root.theme.controlRadius : 8
                                color: threadRowMouse.containsMouse
                                    ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                    : (root.theme ? root.theme.panelColor : "#1c1c20")
                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 8
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        RowLayout {
                                            spacing: 6

                                            Text {
                                                text: modelData.username || "已注销用户"
                                                color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 13
                                                font.bold: true
                                            }

                                            Text {
                                                text: root.timeOf(modelData.last_at)
                                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 11
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: (modelData.last_from_me ? "我说：" : "")
                                                + String(modelData.last_content || "(空消息)")
                                            color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 12
                                        }
                                    }

                                    // 未读槽位定宽：数字出现或消失都不改动别的行。
                                    Text {
                                        Layout.preferredWidth: 54
                                        horizontalAlignment: Text.AlignRight
                                        text: Number(modelData.unread || 0) > 0
                                            ? ("未读 " + modelData.unread) : ""
                                        color: root.theme ? root.theme.warningColor : "#f59e0b"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 11
                                    }
                                }

                                MouseArea {
                                    id: threadRowMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openDm(modelData.peer, modelData.username || "")
                                }
                            }
                        }

                        // ── 私信：单个对话 ──
                        Item {
                            id: dmRoom
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8

                                RowLayout {
                                    spacing: 6

                                    Text {
                                        text: root.dmPeerName.length > 0
                                            ? root.dmPeerName : ("私信 " + root.dmPeerId)
                                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 13
                                        font.bold: true
                                    }

                                    Text {
                                        text: "打开会话会把对方发来的标记成已读"
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        font.pixelSize: 11
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                ListView {
                                    id: dmList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 6
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: root.poolChat ? root.poolChat.threadMessages : []

                                    Text {
                                        anchors.centerIn: parent
                                        visible: dmList.count === 0 && !root.busy
                                        text: "这个会话还没有消息"
                                        color: root.theme ? root.theme.textMutedColor : "#737380"
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    }

                                    // 私信走同一个 holdBottom，开关是 stickDm，只有 landDm 开。
                                    onCountChanged: root.holdBottom(dmList, root.stickDm)
                                    onContentHeightChanged: root.holdBottom(dmList, root.stickDm)
                                    onHeightChanged: root.holdBottom(dmList, root.stickDm)
                                    onMovementStarted: root.stickDm = false
                                    onAtYEndChanged: if (atYEnd) root.stickDm = true
                                    onVisibleChanged: if (visible) root.landDm()

                                    delegate: Item {
                                        id: dmRow
                                        width: dmList.width
                                        height: dmBubble.height + 12
                                        objectName: "dmRow"
                                        readonly property bool mine: root.isMine(modelData)
                                        readonly property bool unreadEdge: root.poolChat
                                            && String(modelData.id || "") === root.poolChat.threadFirstUnreadId
                                        readonly property real bubbleTextMax: Math.max(90, width * 0.62) - 20

                                        Rectangle {
                                            visible: dmRow.unreadEdge
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            height: 1
                                            opacity: 0.75
                                            color: root.theme ? root.theme.accentColor : "#38bdf8"
                                        }

                                        Text {
                                            visible: dmRow.unreadEdge
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.leftMargin: 10
                                            anchors.topMargin: 2
                                            text: "以下为未读"
                                            color: root.theme ? root.theme.accentColor : "#38bdf8"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 10
                                        }

                                        // 方案 B：删除位跟着自己这一侧走 —— 只在自己那条出现，
                                        // 贴在气泡左边；别人的行不占这一格，行尾也不留空列。
                                        Rectangle {
                                            id: dmDeleteSlot
                                            readonly property bool mine: dmRow.mine
                                            readonly property bool armed: root.pendingDeleteId
                                                === String(modelData.id || "")
                                            visible: mine
                                            width: 48
                                            height: 24
                                            x: dmBubble.x - 8 - width
                                            y: dmBubble.y + 4
                                            radius: 6
                                            color: dmDeleteMouse.containsMouse
                                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                                : "transparent"
                                            border.color: armed
                                                ? (root.theme ? root.theme.dangerColor : "#ef4444")
                                                : (root.theme ? root.theme.subtleBorderColor : "#1a1a1e")
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: dmDeleteSlot.armed ? "确认删" : "删除"
                                                color: dmDeleteSlot.armed
                                                    ? (root.theme ? root.theme.dangerColor : "#ef4444")
                                                    : (root.theme ? root.theme.textSecondaryColor : "#a0a0ac")
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 11
                                            }

                                            MouseArea {
                                                id: dmDeleteMouse
                                                anchors.fill: parent
                                                enabled: dmDeleteSlot.mine
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.confirmDelete(modelData.id, true)
                                            }
                                        }

                                        // 整行不用布局引擎：头像贴自己那一侧的行边，
                                        // 气泡贴头像内侧 8px，两条算式写死，谁也不会被挤开。
                                        Item {
                                            id: dmBubbleRow
                                            objectName: "dmBubbleRow"
                                            anchors.fill: parent

                                            Rectangle {
                                                objectName: "dmAvatar"
                                                id: dmAvatar
                                                width: 26
                                                height: 26
                                                y: 6
                                                x: dmRow.mine ? (dmBubbleRow.width - 10 - width) : 10
                                                radius: 13
                                                color: root.theme ? root.theme.accentMutedColor : "#17324d"
                                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                                border.width: 1

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: dmRow.mine ? "我" : root.initialOf(root.dmPeerName)
                                                    color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                }
                                            }

                                            Rectangle {
                                                id: dmBubble
                                                objectName: dmRow.mine ? "dmBubbleMine" : "dmBubbleOther"
                                                // 宽 = 内容宽，最宽到行宽 62%；高 = 内容高。
                                                width: Math.min(dmBubbleCol.implicitWidth + 18,
                                                                Math.max(140, dmBubbleRow.width * 0.62))
                                                height: dmBubbleCol.implicitHeight + 14
                                                y: 6
                                                // 自己的：气泡右边缘 = 头像左边缘 - 8；别人的反过来。
                                                x: dmRow.mine ? (dmAvatar.x - 8 - width) : (dmAvatar.x + dmAvatar.width + 8)
                                                radius: 9
                                                color: dmRow.mine
                                                    ? (root.theme ? root.theme.accentMutedColor : "#14382c")
                                                    : (root.theme ? root.theme.panelColor : "#1c1c20")
                                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                                border.width: 1

                                                ColumnLayout {
                                                    id: dmBubbleCol
                                                    anchors.left: parent.left
                                                    anchors.top: parent.top
                                                    anchors.margins: 9
                                                    spacing: 2

                                                    RowLayout {
                                                        spacing: 6
                                                        layoutDirection: Qt.LeftToRight

                                                        Text {
                                                            text: dmRow.mine ? "我" : root.dmPeerName
                                                            color: root.theme ? root.theme.textSecondaryColor : "#a0a0ac"
                                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                            font.pixelSize: 12
                                                            font.bold: true
                                                        }

                                                        Text {
                                                            text: root.timeOf(modelData.created_at)
                                                            color: root.theme ? root.theme.textMutedColor : "#737380"
                                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                            font.pixelSize: 11
                                                        }
                                                    }

                                                    Text {
                                                        Layout.preferredWidth: Math.min(implicitWidth, dmRow.bubbleTextMax)
                                                        Layout.maximumWidth: dmRow.bubbleTextMax
                                                        visible: String(modelData.content || "").length > 0
                                                        text: modelData.content || ""
                                                        wrapMode: Text.Wrap
                                                        color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 13
                                                    }

                                                    // 收到的动漫卡片：封面 + 片名，和网页版一样是一张卡而不是一行字。
                                                    // 整张卡可点：走 openRequested 那条已经验过的链路
                                                    // （清 activePageOverride -> 切到动漫页 -> 按编号开详情）。
                                                    // 外面套一层 Item，是为了让鼠标层盖住整张卡，
                                                    // 又不会作为额外一格挤进气泡的 ColumnLayout。
                                                    Item {
                                                        id: dmAnimeCard
                                                        visible: !!(modelData.anime && modelData.anime.name)
                                                        // 卡片带的是上游 18 位编号，正好是 loadDetailByKey 吃的键。
                                                        readonly property string key: modelData.anime
                                                            ? String(modelData.anime.id || "") : ""
                                                        implicitWidth: dmAnimeCardRow.implicitWidth
                                                        implicitHeight: dmAnimeCardRow.implicitHeight
                                                        Layout.preferredWidth: implicitWidth
                                                        Layout.preferredHeight: implicitHeight

                                                        RowLayout {
                                                            id: dmAnimeCardRow
                                                            anchors.fill: parent
                                                            spacing: 6

                                                        Rectangle {
                                                            // 海报尺寸：原来 30x38 小得连片名都看不清
                                                            Layout.preferredWidth: 140
                                                            Layout.preferredHeight: 190
                                                            Layout.alignment: Qt.AlignTop
                                                            radius: 3
                                                            color: root.theme ? root.theme.panelRaisedColor : "#26262c"

                                                            Image {
                                                                id: dmCardCover
                                                                anchors.fill: parent
                                                                anchors.margins: 1
                                                                source: modelData.anime ? String(modelData.anime.pic || "") : ""
                                                                asynchronous: true
                                                                fillMode: Image.PreserveAspectCrop
                                                                visible: status === Image.Ready
                                                            }

                                                            Text {
                                                                anchors.centerIn: parent
                                                                visible: !dmCardCover.visible
                                                                text: dmCardCover.status === Image.Error ? "无图" : "…"
                                                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                                                font.pixelSize: 9
                                                            }
                                                        }

                                                        Text {
                                                            Layout.preferredWidth: Math.min(implicitWidth, Math.max(90, dmRow.bubbleTextMax - 152))
                                                            Layout.maximumWidth: Math.max(90, dmRow.bubbleTextMax - 152)
                                                        text: "《" + (modelData.anime ? modelData.anime.name : "") + "》"
                                                        elide: Text.ElideRight
                                                        color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                        font.pixelSize: 17
                                                    }
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            enabled: dmAnimeCard.key.length > 0
                                                            hoverEnabled: true
                                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                            onClicked: root.openRequested(dmAnimeCard.key,
                                                                modelData.anime ? String(modelData.anime.name || "") : "")
                                                        }
                                                    }

                                                    ChatThumbs {
                                                        visible: (modelData.images ? modelData.images.length : 0) > 0
                                                        Layout.preferredWidth: Math.min(implicitWidth, dmRow.bubbleTextMax)
                                                        Layout.maximumWidth: dmRow.bubbleTextMax
                                                        images: modelData.images
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // 待发送的图（已上传拿到直链的）：这一条只在挂了图/在传时占高，
                                // 摘干净就还给消息列表，不会长期挤掉一块版面。
                                Item {
                                    Layout.fillWidth: true
                                    readonly property bool showing: (poolChat ? poolChat.pendingImages.length : 0) > 0
                                        || (poolChat ? poolChat.imageUploading : false)
                                    visible: showing
                                    implicitHeight: showing ? dmPendingRow.implicitHeight + 6 : 0
                                    Layout.preferredHeight: implicitHeight

                                    Row {
                                        id: dmPendingRow
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        spacing: 6

                                        Repeater {
                                            model: root.poolChat ? root.poolChat.pendingImages : []

                                            Rectangle {
                                                id: dmPendingImage
                                                required property string modelData
                                                required property int index
                                                width: 76
                                                height: 76
                                                radius: 6
                                                objectName: "dmPendingImage"
                                                color: root.theme ? root.theme.panelRaisedColor : "#26262c"
                                                border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                                border.width: 1

                                                Image {
                                                    id: dmPendingCover
                                                    anchors.fill: parent
                                                    anchors.margins: 2
                                                    source: dmPendingImage.modelData
                                                    asynchronous: true
                                                    fillMode: Image.PreserveAspectCrop
                                                    visible: status === Image.Ready
                                                }

                                                Text {
                                                    anchors.centerIn: parent
                                                    visible: !dmPendingCover.visible
                                                    text: dmPendingCover.status === Image.Error ? "无图" : "…"
                                                    color: root.theme ? root.theme.textMutedColor : "#737380"
                                                    font.pixelSize: 10
                                                }

                                                // 摘掉这张：点整块就行，不用再瞄一个小 ✕。
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.poolChat.removePendingImage(dmPendingImage.index)
                                                }
                                            }
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: root.poolChat && root.poolChat.imageMessage.length > 0
                                            text: root.poolChat ? root.poolChat.imageMessage : ""
                                            color: root.theme ? root.theme.textMutedColor : "#858585"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                Rectangle {
                                    id: dmInputRow
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    radius: root.theme ? root.theme.controlRadius : 8
                                    color: root.theme ? root.theme.panelColor : "#1c1c20"
                                    border.color: dmInput.activeFocus
                                        ? (root.theme ? root.theme.accentColor : "#38bdf8")
                                        : (root.theme ? root.theme.subtleBorderColor : "#1a1a1e")
                                    border.width: 1

                                    AnimePicker { field: dmInput }
                                    EmojiPicker { id: dmEmoji; field: dmInput }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 6
                                        spacing: 8

                                        // 挂了待发送的动漫卡片就在这里亮一颗胶囊，点 ✕ 摘掉。
                                        Rectangle {
                                            visible: root.pendingAnimeText.length > 0
                                            Layout.preferredHeight: 24
                                            radius: 12
                                            color: root.theme ? root.theme.accentMutedColor : "#0e263a"
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1

                                            Row {
                                                anchors.centerIn: parent
                                                anchors.margins: 8
                                                spacing: 6

                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "《" + root.pendingAnimeText + "》"
                                                    color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                    font.pixelSize: 11
                                                }

                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "✕"
                                                    color: root.theme ? root.theme.textMutedColor : "#737380"
                                                    font.pixelSize: 11
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: if (root.poolChat)
                                                    root.poolChat.clearAnime()
                                            }
                                        }

                                        TextInput {
                                            id: dmInput
                                            Layout.fillWidth: true
                                            color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 13
                                            selectByMouse: true
                                            clip: true
                                            verticalAlignment: TextInput.AlignVCenter
                                            maximumLength: root.draftLimit
                                            onTextChanged: root.trackAnime(text)
                                            // 焦点在输入框里时，Ctrl+V 会被 TextInput 自己吃掉，
                                            // 窗口级 Shortcut 收不到 —— 必须在这里先拦一道。
                                            Keys.onPressed: (event) => {
                                                if (event.key === Qt.Key_V
                                                        && (event.modifiers & Qt.ControlModifier)
                                                        && root.poolChat
                                                        && root.poolChat.clipboardHasImage) {
                                                    root.poolChat.pasteImageFromClipboard()
                                                    event.accepted = true
                                                }
                                            }
                                            onAccepted: root.sendDraft(text, true)

                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                visible: !dmInput.text && !dmInput.activeFocus
                                                text: "给 " + (root.dmPeerName.length > 0 ? root.dmPeerName : "对方") + " 发私信"
                                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 13
                                            }
                                        }

                                        Text {
                                            Layout.preferredWidth: 56
                                            horizontalAlignment: Text.AlignRight
                                            text: root.pointCount(dmInput.text) + "/" + root.draftLimit
                                            color: root.pointCount(dmInput.text) > root.draftLimit
                                                ? (root.theme ? root.theme.warningColor : "#f59e0b")
                                                : (root.theme ? root.theme.textMutedColor : "#737380")
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.pixelSize: 11
                                        }

                                        // 表情：和群聊那一侧同一个面板、同一份码点表。
                                        Rectangle {
                                            Layout.preferredWidth: 44
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: dmEmojiMouse.containsMouse
                                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                                : (dmEmoji.open
                                                    ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                                                    : "transparent")
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: "表情"
                                                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 12
                                            }

                                            MouseArea {
                                                id: dmEmojiMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: dmEmoji.open = !dmEmoji.open
                                            }
                                        }

                                        // 选图 → 逐张上传拿直链，挂在待发送里。
                                        Rectangle {
                                            Layout.preferredWidth: 40
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: dmInputPickMouse.containsMouse
                                                ? (root.theme ? root.theme.panelRaisedColor : "#26262c")
                                                : "transparent"
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1
                                            enabled: !(root.poolChat && root.poolChat.imageUploading)

                                            Text {
                                                anchors.centerIn: parent
                                                text: root.poolChat && root.poolChat.imageUploading ? "传中" : "图"
                                                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 12
                                            }

                                            MouseArea {
                                                id: dmInputPickMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: if (root.poolChat) root.poolChat.chooseAndUploadImages()
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 52
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: dmSendMouse.containsMouse
                                                ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                                                : "transparent"
                                            border.color: root.theme ? root.theme.subtleBorderColor : "#1a1a1e"
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: "发送"
                                                color: root.theme ? root.theme.accentColor : "#38bdf8"
                                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                                font.pixelSize: 12
                                            }

                                            MouseArea {
                                                id: dmSendMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.sendDraft(dmInput.text, true)
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
    }

    // 一条消息最多带 10 张图：88 方块排开，撞到气泡上限就换行，点开看大图。
    component ChatThumbs: Flow {
        id: chatThumbs
        required property var images
        spacing: 4

        Repeater {
            model: chatThumbs.images

            Rectangle {
                id: thumb
                required property var modelData
                width: 140
                height: 140
                radius: 6
                objectName: "chatThumb"
                color: root.theme ? root.theme.panelRaisedColor : "#26262c"
                border.color: thumbMouse.containsMouse
                    ? (root.theme ? root.theme.accentColor : "#38bdf8")
                    : (root.theme ? root.theme.subtleBorderColor : "#1a1a1e")
                border.width: 1

                Image {
                    id: thumbImage
                    anchors.fill: parent
                    anchors.margins: 2
                    source: String(thumb.modelData || "")
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                }

                // 没ready就一直有句话：空白方块会让人以为功能没做，而不是图没到。
                Text {
                    anchors.centerIn: parent
                    visible: !thumbImage.visible
                    text: thumbImage.status === Image.Error ? "图没到" : "载入中"
                    color: root.theme ? root.theme.textMutedColor : "#737380"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 11
                }

                MouseArea {
                    id: thumbMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.showImage(String(thumb.modelData || ""))
                }
            }
        }
    }
    // ##片名## 的候选浮层：贴在所属输入行的上沿（和 @ 浮层同一个做法），
    // 点一条就挂上卡片、并把输入框里那段 ##…## 抹掉。
    component AnimePicker: Rectangle {
        id: animePicker
        required property var field
        readonly property var results: root.poolChat ? root.poolChat.animeResults : []
        readonly property bool searching: root.poolChat ? root.poolChat.animeLoading : false
        visible: root.animeKeyword.length > 0 && field.activeFocus
            && (results.length > 0 || searching)
        anchors.left: parent.left
        anchors.bottom: parent.top
        width: parent.width
        height: animePickerColumn.implicitHeight + 12
        z: 21
        radius: 6
        color: root.theme ? root.theme.panelRaisedColor : "#26262c"
        border.color: root.theme ? root.theme.borderColor : "#2a2a30"
        border.width: 1

        Column {
            id: animePickerColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 2

            Text {
                visible: animePicker.searching && animePicker.results.length === 0
                text: "在找《" + root.animeKeyword + "》…"
                color: root.theme ? root.theme.textMutedColor : "#737380"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: 11
            }

            Repeater {
                model: animePicker.results

                Rectangle {
                    width: animePickerColumn.width
                    height: 38
                    radius: 4
                    color: animePickMouse.containsMouse
                        ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                        : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Rectangle {
                            width: 26
                            height: 34
                            radius: 3
                            color: root.theme ? root.theme.panelRaisedColor : "#26262c"

                            Image {
                                id: resultCover
                                anchors.fill: parent
                                anchors.margins: 1
                                source: String(animePicker.results[index].pic || "")
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                visible: status === Image.Ready
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: !resultCover.visible
                                text: resultCover.status === Image.Error ? "无图" : "…"
                                color: root.theme ? root.theme.textMutedColor : "#737380"
                                font.pixelSize: 9
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(animePicker.results[index].name || "")
                            color: root.theme ? root.theme.textPrimaryColor : "#f0f0f5"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        id: animePickMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.pickAnimeAt(index, animePicker.field)
                    }
                }
            }
        }
    }

    // 大图覆盖层：只盖住「我的」这一页，点任意处或按 Esc 之外的一切都不动别的界面。
    // 表情面板：浮在输入行上方的一张网格，点一下把那个表情插到光标处。
    // 名单写的是码点而不是 emoji 本身 —— emoji 字符经我这一手转录会静默丢字节，
    // 用 String.fromCodePoint 现拼既看得懂、也不会被改掉。
    component EmojiPicker: Rectangle {
        id: emojiPicker
        required property var field
        property bool open: false
        visible: open
        anchors.left: parent.left
        anchors.bottom: parent.top
        width: parent.width
        height: emojiGrid.implicitHeight + 30
        z: 22
        radius: 6
        color: root.theme ? root.theme.panelRaisedColor : "#26262c"
        border.color: root.theme ? root.theme.borderColor : "#2a2a30"
        border.width: 1

        readonly property var faces: [
            0x1F600, 0x1F603, 0x1F604, 0x1F601, 0x1F606, 0x1F605, 0x1F602, 0x1F923,
            0x1F642, 0x1F643, 0x1F609, 0x1F60A, 0x1F607, 0x1F970, 0x1F60D, 0x1F618,
            0x1F61B, 0x1F61C, 0x1F61D, 0x1F929, 0x1F92A, 0x1F914, 0x1F928, 0x1F610,
            0x1F615, 0x1F644, 0x1F62C, 0x1F92F, 0x1F634, 0x1F60E, 0x1F913, 0x1F633,
            0x1F641, 0x1F622, 0x1F62D, 0x1F631, 0x1F616, 0x1F623, 0x1F614, 0x1F972,
            0x1F44D, 0x1F44E, 0x1F44F, 0x1F64C, 0x1F64F, 0x1F4AA, 0x2764, 0x1F494,
            0x1F525, 0x1F389, 0x1F31F, 0x2B50, 0x1F4AF, 0x1F62B
        ]

        Text {
            id: emojiTitle
            anchors.left: parent.left
            anchors.leftMargin: 9
            anchors.top: parent.top
            anchors.topMargin: 6
            text: "表情"
            color: root.theme ? root.theme.textMutedColor : "#737380"
            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
            font.pixelSize: 11
        }

        Rectangle {
            id: emojiClose
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 5
            anchors.topMargin: 4
            width: 20
            height: 16
            radius: 4
            color: emojiCloseMouse.containsMouse
                ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                : "transparent"

            Text {
                anchors.centerIn: parent
                text: "✕"
                color: root.theme ? root.theme.textMutedColor : "#737380"
                font.pixelSize: 11
            }

            MouseArea {
                id: emojiCloseMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: emojiPicker.open = false
            }
        }

        Grid {
            id: emojiGrid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: emojiTitle.bottom
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            anchors.topMargin: 4
            columns: 12
            rowSpacing: 2
            columnSpacing: 2

            Repeater {
                model: emojiPicker.faces

                Rectangle {
                    id: emojiCell
                    required property var modelData
                    width: 30
                    height: 27
                    radius: 4
                    color: emojiCellMouse.containsMouse
                        ? (root.theme ? root.theme.accentMutedColor : "#0e263a")
                        : "transparent"

                    Text {
                        anchors.centerIn: parent
                        // Segoe UI Emoji 是彩色位图字体，只有 NativeRendering 走系统 DirectWrite 才有颜色。
                        text: String.fromCodePoint(Number(emojiCell.modelData))
                        font.family: "Segoe UI Emoji"
                        font.pixelSize: 17
                        renderType: Text.NativeRendering
                    }

                    MouseArea {
                        id: emojiCellMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            emojiPicker.field.insert(emojiPicker.field.cursorPosition,
                                                     String.fromCodePoint(Number(emojiCell.modelData)))
                            emojiPicker.field.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: imagePreview
        anchors.fill: parent
        z: 60
        visible: root.previewUrl.length > 0
        color: "#D8000000"

        Image {
            id: previewImage
            anchors.fill: parent
            anchors.margins: 26
            source: root.previewUrl
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
            visible: status === Image.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: !previewImage.visible
            text: previewImage.status === Image.Error
                ? "这张图打不开：" + root.previewUrl
                : "原图加载中…"
            wrapMode: Text.Wrap
            maximumLineCount: 3
            color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
            font.pixelSize: 13
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 8
            text: "点任意处关闭"
            color: root.theme ? root.theme.textMutedColor : "#858585"
            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
            font.pixelSize: 12
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.previewUrl = ""
        }
    }

    Component.onCompleted: {
        reload()
        syncPolling()
    }
}
