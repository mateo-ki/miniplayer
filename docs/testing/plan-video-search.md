# Plan: 视频搜索 + 左侧导航栏

## 目标
1. 左侧导航栏：两个入口 — "视频搜索" 和 "播放器"
2. 视频搜索页：关键词搜索 + 按 ID 查询，显示搜索结果列表
3. API 管理：可添加/删除/修改 API Base URL
4. 视频详情页：点击搜索结果 → 显示详情（封面、简介、演员等）→ 选择线路播放

## 架构设计

### C++ 层

#### 1. `VideoSearchModel` (`src/models/VideoSearchModel.h/.cpp`)
搜索结果列表模型，继承 `QAbstractListModel`。
- **Roles**: `VodIdRole`, `VodNameRole`, `VodPicRole`, `VodRemarksRole`, `VodYearRole`, `VodAreaRole`, `VodClassRole`, `VodActorRole`, `VodDirectorRole`, `VodBlurbRole`, `VodPlayFromRole`, `VodPlayUrlRole`, `VodScoreRole`
- **Q_INVOKABLE**: `search(baseUrl, keyword)`, `searchById(baseUrl, ids)`, `clear()`
- 内部使用 `QNetworkAccessManager` 异步请求，解析 JSON 后 `beginResetModel/endResetModel`

#### 2. `ApiSiteModel` (`src/models/ApiSiteModel.h/.cpp`)
API 站点管理模型，继承 `QAbstractListModel`。
- **Roles**: `NameRole`, `BaseUrlRole`
- **Q_INVOKABLE**: `add(name, url)`, `removeAt(index)`, `update(index, name, url)`
- **持久化**: 使用 `QSettings` 存储站点列表
- 预置默认站点: `{"mtzy2", "https://mtzy2.com/provide/vod"}`

#### 3. `PlayerController` 扩展
新增 Q_PROPERTY:
- `VideoSearchModel *videoSearchModel`
- `ApiSiteModel *apiSiteModel`

新增 Q_INVOKABLE:
- `searchVideos(keyword)` — 使用当前选中的 API 站点搜索
- `searchVideoById(vodId)` — 按 ID 查询详情
- `playVideoUrl(url)` — 直接播放 m3u8 链接

### QML 层

#### 4. 布局改造 `Main.qml`
```
ApplicationWindow
  +-- RowLayout
  |     +-- NavigationSidebar (64px wide, 图标+文字)
  |     +-- StackLayout (currentIndex 绑定 sidebar)
  |           +-- PlayerView (现有 VideoSurfacePane + PlaybackControlBar)
  |           +-- SearchView (搜索页)
```

#### 5. `NavigationSidebar.qml` (`ui/qml/components/`)
垂直排列的导航图标栏，两个按钮：
- 🔍 视频搜索
- ▶ 播放器
选中项高亮（accent color）

#### 6. `SearchView.qml` (`ui/qml/components/`)
搜索页面，包含：
- 顶部：API 站点选择器（下拉框 + 管理按钮）+ 搜索输入框 + 搜索按钮
- 中部：搜索结果网格/列表（封面缩略图 + 标题 + 备注 + 年份 + 评分）
- 点击结果 → 展开详情面板

#### 7. `VideoDetailPanel.qml` (`ui/qml/components/`)
视频详情面板（覆盖在搜索页上方）：
- 封面大图
- 标题、年份、地区、类型、导演、演员、简介
- 播放线路选择（从 `vod_play_from` 解析）
- 选集列表（从 `vod_play_url` 解析）
- 点击集数 → 调用 `playerController.playVideoUrl(url)` → 切换到播放器页

#### 8. `ApiManagerDialog.qml` (`ui/qml/components/`)
API 站点管理弹窗：
- 列表显示所有站点（名称 + URL）
- 添加按钮 → 输入名称和 URL
- 编辑按钮 → 修改选中站点
- 删除按钮 → 删除选中站点

### CMakeLists.txt 变更
在 `qt_add_qml_module` 中添加新 QML 文件：
- `ui/qml/components/NavigationSidebar.qml`
- `ui/qml/components/SearchView.qml`
- `ui/qml/components/VideoDetailPanel.qml`
- `ui/qml/components/ApiManagerDialog.qml`

在 `qt_add_executable` 中添加新 C++ 文件：
- `src/models/VideoSearchModel.cpp`
- `src/models/ApiSiteModel.cpp`

### API 调用细节

**关键词搜索**: `GET {baseUrl}?ac=list&wd={keyword}&pg=1`
**ID 详情查询**: `GET {baseUrl}?ac=detail&ids={id}`
**列表浏览**: `GET {baseUrl}?ac=list&pg={page}`

解析 `vod_play_url` 格式:
- 线路用 `$$$` 分隔
- 每个线路内，集数用 `#` 分隔
- 每集格式: `集名$链接`
- 示例: `正片$https://xxx.m3u8$$$正片$https://xxx/share/xxx`

## 实现顺序

1. **ApiSiteModel** — C++ 模型 + QSettings 持久化
2. **VideoSearchModel** — C++ 模型 + 网络请求 + JSON 解析
3. **PlayerController 扩展** — 注册新模型 + 新方法
4. **NavigationSidebar** — QML 导航栏组件
5. **Main.qml 重构** — RowLayout + StackLayout
6. **SearchView** — 搜索页面
7. **VideoDetailPanel** — 详情面板
8. **ApiManagerDialog** — API 管理弹窗
9. **CMakeLists.txt** — 注册新文件
