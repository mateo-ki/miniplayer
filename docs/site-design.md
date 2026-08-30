# 远程加载站点设计

## Bee 蜜蜂菜单设计（增补待确认）

### 分层
- `src/media/BeeClient.{h,cpp}`：Bee HTTP 请求、AES 解密、推荐/搜索/详情响应解析、剧集 URL 解析。
- `src/models/BeeVideoModel.{h,cpp}`：Qt model + Q_PROPERTY，管理推荐或搜索结果、当前列表项、详情、loading/error。
- `ui/qml/components/BeeView.qml`：独立列表页面，复用普通视频列表的卡片语言，不共享 Dmghg 状态。
- `ui/qml/components/VideoDetailPanel.qml`：蜜蜂详情直接复用普通视频详情组件，避免两套详情交互漂移。
- `src/controller/PlayerController`：仅复用现有 `playVodUrl(QString)`，不新增播放引擎。
- `NavigationSidebar.qml/Main.qml`：增加 Bee 菜单项和页面索引。

### Bee 数据映射
- 推荐列表：从 `/film-recommend-model/select` 解密后的根对象 `data` 读取 `vod_id/vod_name/vod_pic/vod_remarks/vod_year/vod_area/vod_class/vod_blurb/vod_version`。
- 搜索列表：`vod_id -> vodId`，`vod_name -> vodName`，`vod_pic -> vodPic`，`vod_remarks -> vodRemarks`，`type_name -> typeName`。
- 详情：保留 `vod_name/vod_content/vod_year/vod_area/vod_class/vod_remarks/vod_play_from/vod_play_url` 等展示和播放字段。
- 详情合并：以当前列表项为基础，再覆盖详情接口返回的非空字段；因为 `/vod/play` 不返回 `vod_pic`，列表海报必须被保留。
- 选集：解析 `vod_play_url` 的每行 `episode$url` 为 `{name,url}`；UI 使用 name，播放使用 url。

### 解密
- AES-256-CBC，KEY=`AD42F8897B035B751599513577538520`，IV=`8866668815935700`。
- 搜索响应密文字段为 `data`，详情响应密文字段为 `list`。
- 推荐响应本身是 `text/plain` Base64 密文，解密后是包含 `data` 数组的 JSON 对象，不经过搜索/详情信封解析。
- 兼容响应直接返回 JSON 或字段为空的错误信封；禁止把错误信封当作空列表。

### 页面与导航
- 新页面使用新的 page index，避免复用 11（Dmghg）。
- 侧边栏标签“蜜蜂”，图标沿用现有文本图标体系。
- `BeeView` 首次显示且模型为空时调用 `loadRecommended()`；搜索框为空时同样回到推荐列表。
- 顶部搜索栏采用弹性宽度，窄窗口不允许把输入框和搜索按钮挤出页面。
- 点击列表卡片先记录当前列表项，再异步加载详情；详情完成后把合并数据传给 `VideoDetailPanel`。
- Bee 播放成功后切换到播放器页，但保留 `BeeView` 的详情状态，返回时可继续选集。

### Windows 图片运行时
- `imageformats/qjpeg[d].dll` 依赖同配置的 `jpeg62.dll`，必须从 `_miniPlayerQtBinDir` 复制，Debug 不得错误混用 Release 运行库。
- `imageformats/qwebp[d].dll` 与 `libwebp/libwebpdecoder/libwebpdemux/libwebpmux/libsharpyuv` 一起部署。
- 打包检查同时校验 JPEG/WebP 插件及关键传递依赖，避免构建成功但运行时报告 `Unsupported image format`。

## 页面设计

在 `SiteManagementView.qml` 顶部标题栏现有操作按钮旁增加“加载站点”按钮。按钮使用现有 `ActionButton` 样式，加载期间显示“加载中...”并禁用，结果通过页面已有 `message` 区域反馈。

## 模型接口

在 `ApiSiteModel` 增加：

- `remoteSitesLoading` 只读属性，用于控制按钮状态。
- `loadRemoteSites()` 可调用方法，用于下载固定站点源。
- `remoteSitesLoadingChanged` 和 `remoteSitesLoadFinished(message)` 信号。

远程地址作为模型层常量保存，避免在 QML 中硬编码网络协议细节。实际请求使用现有 Qt Network 能力，并启用安全重定向策略和超时。

## 导入复用

将现有剪贴板导入中的“解析分享串并合并站点”提取为内部方法，例如 `importSitesFromText(const QString &text)`。剪贴板导入和远程导入都调用该方法，确保两条路径的解密、校验、更新、追加和持久化行为一致。

## 错误处理

- 同一时间只允许一个远程加载请求。
- 仅接受请求成功且非空的文本内容。
- 设置合理的响应大小上限，避免异常大文件占用内存。
- 解析和校验完全成功后才合并数据，失败时不修改模型。
- 请求结束后统一恢复加载状态并向 QML 返回结果消息。

## 测试设计

- 为文本导入与重复 URL 更新规则增加模型单元测试。
- 为页面按钮、加载状态绑定及固定远端入口增加集成断言。
- 编译 `MeloBox` 和模型测试目标。
- 启动应用后用 Playwright 检查页面、控制台、网络请求和按钮交互。

## 站点排序设计

`ApiSiteModel` 增加 `moveSite(from, to)` 接口，使用模型移动通知同步移动以下数据：

- 站点主体数据。
- 分享选择状态。
- 站点检测状态和延迟。
- 当前选中站点索引。

移动成功后立即保存配置。QML 列表代理增加拖动手柄，拖动结束时根据列表目标位置调用模型移动接口。搜索或类型筛选生效时禁用拖动，避免过滤列表位置与完整模型位置之间产生含糊映射。

## 优质站点设计

`ApiSite` 增加布尔字段 `premium`，默认值为 `false`。模型增加对应角色、读取接口和切换接口。列表使用紧凑的“优质”徽标展示，并提供可直接切换的标志按钮。

本地 JSON 和加密分享站点对象增加 `premium` 字段。读取旧数据时字段缺失按 `false` 处理；导入相同 URL 的站点时，远程分享中的优质标记同步更新。

优质标记只表达站点质量，不隐式修改用户手动排序。

## JSON 视频站点导入设计

`ApiSiteModel` 增加独立的 JSON 加载状态和完成信号。模型异步下载固定 JSON 地址，限制响应大小并校验 HTTP 状态，然后解析顶层数组中的 `name` 和 `api`。

导入使用规范化 URL 建立索引：JSON 内部重复 URL 跳过；已存在的 URL 更新名称并设为 `video`，同时保留原优质标记和用户排序；新 URL 追加。导入保存后，对所有有效唯一 URL 对应的模型行调用现有 `refreshSiteStatusAt()`。
## 站点检测更新与限流

批量检测通过待检测队列调度，最多保留 4 个活动请求。请求完成、超时或 URL 无效时释放活动槽位并继续下一项；手动单站检测仍立即执行。

站点页收到 `dataChanged` 后只同步受影响的源模型行，不再清空并重建过滤列表。主窗口改为监听专用的 `siteContentChanged` 信号，只有站点名称、地址或类型变化时重建短视频站点缓存，检测状态与分享勾选等行状态变化不会触发重建。

## 站点拖动与边缘滚动

拖动手柄使用独立鼠标捕获，不再直接修改由 `ListView` 布局管理的代理坐标。拖动期间通过 `Translate` 显示行的视觉位置，并暂停列表自身的 Flick 操作；释放时根据列表内容坐标计算目标索引，再调用模型的 `moveSite()`。

拖动期间以 16 毫秒定时器检测指针是否进入列表上下边缘区域。进入边缘后按照靠近边缘的程度调整 `contentY`，并限制在有效滚动范围内，使站点可以拖过当前可视区域。
## 站点批量删除

`ApiSiteModel::removeSelectedSites()` 复用现有 `shareSelected_` 勾选状态。删除时先构建保留站点及其关联状态的新数组，再通过一次模型重置完成更新，避免逐行删除导致重复刷新。当前站点通过旧索引映射保留；若当前站点被删除，则优先选择其后相邻站点，没有后项时选择前一项。

JSON 视频站点导入继续保留在模型层，但站点管理页不再创建对应操作按钮。
## 优质站点置顶

模型使用稳定分组方式重排站点：先按原顺序收集所有优质站点，再按原顺序收集普通站点。站点主体、分享勾选、检测状态和延迟数组使用同一旧索引映射同步移动，当前站点索引也通过映射恢复。拖动目标索引按照优质分组边界进行限制。
## 下拉选中标记与精确插入

视频站点下拉的选中标记使用 18px 深绿色圆形底、细绿色边框和浅绿色勾号，未选中项保留同等布局宽度以避免文字左右跳动。

拖动释放时使用指针的内容坐标定位目标行，并以行高的一半作为前插或后插分界。目标表示为插入槽位；当槽位位于源项之后时减一，抵消源项从列表移除后的索引变化。
## 文本编码与控制栏热区

语音和表情页面直接修复 QML 中已经错误转码的 UTF-8 字面文本，并继续使用 `AppStrings` 中已有的统一标题与按钮文案。视频控制栏新增独立的 `playerControlHotZoneHeight` 常量，普通播放模式的透明触发窗口和底部提示条统一使用 32px，控制栏组件仍使用主题定义的高度。
## 普通视频手势与会话恢复

普通视频使用与主窗口同级的透明手势窗口覆盖原生 mpv 子窗口，负责双击、长按、滚轮和控制栏唤出事件。长按开始时复用现有 3 倍速函数，释放、取消或窗口隐藏时恢复原速。

控制器在首次进入短视频前保存普通视频 URL、进度和播放状态。短视频停止后重新加载保存的 URL，并在媒体报告时长或播放位置后恢复 seek 和暂停/播放状态，避免加载尚未就绪时过早定位。
# 视频类型二级分类设计

## 数据模型

`VideoSearchModel` 解析分类数组时，除现有 `typeId`、`typeName` 外增加 `parentTypeId`，数据来自接口的 `type_pid`。缺失、空值或 `0` 统一视为一级类型。

## 页面状态

`SearchView` 增加当前一级类型 ID，并将分类数据整理为：

- 一级分类：`parentTypeId` 为空或为 `0`。
- 二级分类：`parentTypeId` 等于当前一级类型 ID。
- 无父子字段的站点：全部分类继续作为单层一级分类显示。

第一行保留现有横向滚动类型栏。点击有子分类的一级类型后，在该标签下方打开非模态小型 `Popup`，弹框内横向展示二级类型。此时只更新展开状态，不修改实际筛选 ID，也不请求列表；点击二级类型后，才将该二级类型自己的 `typeId` 写入 `selectedTypeId`，并通过现有 `loadVideoListByCategory()` 加载。

“全部”和没有子分类的单层类型不显示弹框，继续直接加载对应列表。

## 状态重置

切换站点、关键词搜索、选择“全部”以及分类数据更新后，重新校验一级和二级选中项。不存在的选中项自动清空，避免把上一个站点的类型 ID 带到新站点。

## 兼容性

部分资源站只返回平铺分类或不返回 `type_pid`。此时不显示第二行，行为与当前版本一致。

# 视频页面优质站点标记

`SearchView` 构建视频站点缓存时读取 `ApiSiteModel::premiumAt()`，将结果保存为 `sitePremium`。站点下拉代理在名称与地址右侧显示紧凑的“优质”徽标，使用深金色背景、细边框和浅金色文字，与绿色当前站点标记分工明确。

# 视频播放控制栏覆盖交互

`VideoPlayerView` 不再为控制栏设置底部边距，mpv 视频几何覆盖完整播放区域。普通和沉浸模式的控制窗口继续作为透明顶层窗口覆盖在视频底部，隐藏状态只保留透明鼠标热区，不绘制背景。

新增立即隐藏路径：鼠标从热区、控制栏或视频底部区域移出时停止自动隐藏计时器并直接清除 `bottomControlReveal`。进度条正在拖动或后端仍在 seek 时暂不隐藏，操作结束后再由离开事件收起。

# 返回详情按钮避让导航栏

返回详情按钮是独立顶层窗口，使用桌面坐标定位。横向位置始终根据 `sidebar.visible` 决定是否增加导航栏宽度，不再把导航栏避让与 `immersiveMode` 绑定。这样非全屏铺满画面时，左侧菜单仍显示，按钮也会从菜单右侧开始布局。
