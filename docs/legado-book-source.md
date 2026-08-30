# 书源（Legado 阅读）模块

在「阅读」菜单中导入并使用 [Legado（阅读 3.x）](https://github.com/gedoor/legado)书源，实现搜索、详情、目录、正文阅读与听书播放。书源 JSON 支持 yckceo 等站点的合集链接（单个 URL 可含数百个源）。

## 使用

1. 侧边栏进入「📖 阅读」。
2. 点击「导入」展开面板，粘贴书源链接（如 `https://www.yckceo.com/yuedu/shuyuans/json/id/1248.json`）或书源 JSON 文本，导入后自动持久化到本地。
3. 左上角下拉框选择书源；合集内失效源很多，搜索失败时换一个源即可。
4. 搜索或用「发现」下拉加载列表 → 点击书籍看详情 → 「目录」→ 章节阅读。
5. `bookSourceType == 1`（音频）的源点章节后直接交给播放器播放；`== 2`（图片/漫画）以图片流展示。

## 代码结构

```
src/book/BookSource.{h,cpp}      书源数据结构与 JSON 解析
src/book/HtmlDom.{h,cpp}         容错 HTML DOM（默认查询规则的载体）
src/book/RuleEngine.{h,cpp}      规则引擎（见下）
src/book/LegadoClient.{h,cpp}    AnalyzeUrl 等价物 + 同步网络请求
src/models/LegadoSourceModel.*   书源导入/列表/持久化/选中
src/models/LegadoBookModel.*     搜索/详情/目录/正文流程与音频播放信号
ui/qml/components/BookView.qml   阅读页面
```

规则引擎基于 QJSEngine，支持：

- `@js:` / `<js>...</js>`，绑定 `result/baseUrl/key/page/title/source/java/cookie`，
  预载书源 `jsLib`，支持 `source.getVariable/setVariable/put/get`
- JSONPath：`$.a.b[0]`、`$.data[*]`、`$..name`、`@Json:` 前缀
- JSoup 默认查询：`class.x`/`id.x`/`tag.x`/`.x`/`#x`/`[n]`/`children`/
  `text`/`textNodes`/`ownText`/`html`/属性名；JSON 节点上的裸字段名
- `@css:` 简化选择器（空格后代、`>` 直接子级）
- `##` 正则替换（Java 风格 `$N` 反向引用）、`||` 回退、`&&` 合并
- `{{key}}`/`{{page}}`/`{{$.字段}}`/`{{JS 表达式}}` 模板
- searchUrl 的 `,{...}` 请求选项（method/body/charset/headers）、相对地址补全

不支持 XPath（返回空并记录日志）；`java.ajax/get/post` 提供同步请求。

## 已知限制

- 依赖重型 JS/浏览器渲染的源无法工作
- XPath 规则源不支持
- 正文翻页 `nextContentUrl` 最多跟随 10 跳，目录 `nextTocUrl` 最多 40 跳

## 测试

```powershell
# 规则引擎单元测试（17 项）
ctest -C Debug -R miniPlayerBookTests

# 真实网络端到端（导入 → 选源 → 搜索 → 详情 → 目录 → 正文）
$env:MELOBOX_LEGADO_E2E=1
$env:MELOBOX_LEGADO_JSON="<书源合集 json 路径>"
build\Debug\miniPlayerBookTests.exe legadoEndToEnd
```
