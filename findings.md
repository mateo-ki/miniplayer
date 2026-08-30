# Bee 接入调研记录

## 文档来源
- `D:/project/ida/dm/bee`

## 当前发现
- Bee 业务网关为 `https://java.beeweb.cc`，解析/文件域涉及 `https://beeweb.cc`、`https://xmfzy.beeweb.cc`。
- 搜索接口：`GET /vod/search?wd=<关键字>`，加密载荷位于响应 `data` 字段。
- 详情接口：`GET /vod/play?id=<vod_id>`，加密载荷位于响应 `list` 字段。
- 搜索结果不含播放列表；必须用 `vod_id` 请求详情并读取 `vod_play_url`。
- `vod_play_url` 由多行 `集名$url` 组成，部分地址可直接播放 HLS/MP4。
- Bee 与现有 Dmghg 的网关、鉴权、解密和数据结构均不同，应独立实现客户端和模型。
- UI 可复用现有动漫页面的搜索网格、详情、选集和播放器信号模式，但菜单入口与模型状态应独立。

## 风险与待确认项
- 需要完整核对 `bee_aes.py` 的解密算法、请求头和是否存在列表浏览接口。
- 需要确认“新增菜单 Bee”是独立侧边栏入口，还是动漫页面内的数据源切换；当前倾向独立侧边栏入口。
