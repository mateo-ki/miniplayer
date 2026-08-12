# miniPlayer 播放流程设计

## 问题总结

当前存在以下问题：
1. **seek 后进度条回弹**：`AVSEEK_FLAG_BACKWARD` 导致实际播放位置在 seek 目标之前的关键帧处
2. **seek 后播放从头开始**：旧管线的 `finished` 信号异步到达，重置了状态
3. **seek 后视频卡住**：音频时钟超前于视频，所有视频帧被丢弃
4. **播放结束后仍有声音**：DemuxWorker EOF 后音频队列没有正确关闭

## 核心原则

1. **音频时钟 = 实际解码帧的 PTS**，不使用字节计数
2. **进度条 = 实际播放位置**，不是 seek 目标
3. **seek 后音视频都从关键帧开始**，保持同步
4. **管线生命周期清晰**：seek 只停止管线，play 负责启动

---

## 一、文件打开流程

```
open(path)
├── session_.open(path)           // 打开 AVFormatContext
├── findStreams()                  // 找到 audio/video stream index
├── audioDecoder_.initialize()     // 初始化音频解码器
├── videoDecoder_.initialize()     // 初始化视频解码器
├── extractor_.extract()           // 提取媒体信息
├── presentFirstFrame()            // 解码并显示第一帧
│   ├── av_read_frame 循环找视频包
│   ├── videoDecoder_.decode()     // 解码第一帧
│   ├── videoBridge_->present()    // 显示
│   ├── av_seek_frame(ctx, 0)      // 回到开头
│   └── videoDecoder_.flush()
└── state_ = Idle
```

**注意**：`presentFirstFrame()` 后文件位置在开头，解码器已 flush。

---

## 二、Seek 流程

```
seek(positionMs)
├── stopPipeline()                 // 停止当前管线（如果在运行）
├── audioDecoder_.flush()          // 清空音频解码器缓冲
├── videoDecoder_.flush()          // 清空视频解码器缓冲
├── av_seek_frame(target, BACKWARD) // 定位到关键帧 T' ≤ T
├── 解码并显示 T' 处的视频帧（预览）
│   ├── av_read_frame 循环找视频包
│   ├── videoDecoder_.decode()
│   └── videoBridge_->present()
├── av_seek_frame(target, BACKWARD) // 回到 T'（管线从这里开始）
├── videoDecoder_.flush()          // 清空解码器（预览用的帧不需要了）
├── pendingSeekSec_ = T / 1000.0   // 记录 seek 目标（仅用于日志）
├── state_ = Paused
└── emit stateChanged()
```

**关键点**：
- seek 只做"定位 + 预览"，不启动管线
- 管线由 `play()` 负责启动
- 文件位置在 T'（关键帧），不是 T（seek 目标）

---

## 三、Play 流程

### 3.1 从 Paused 状态播放（seek 后或暂停后）

```
play() [state == Paused]
├── if audioOutput_ 存在（暂停后恢复）
│   ├── resume queues
│   ├── audioOutput_->resume()
│   └── syncTimer_->start()
├── else（seek 后首次播放）
│   ├── startPipeline()
│   ├── state_ = Playing
│   └── syncTimer_->start()
└── emit stateChanged()
```

### 3.2 从 Idle 状态播放（首次或 EOF 后）

```
play() [state == Idle]
├── if EOF 后重播
│   ├── av_seek_frame(0, BACKWARD)  // 回到开头
│   ├── audioDecoder_.flush()
│   └── videoDecoder_.flush()
├── startPipeline()
├── state_ = Playing
├── syncTimer_->start()
└── emit stateChanged()
```

### 3.3 startPipeline() 流程

```
startPipeline()
├── 创建 DemuxWorker
│   ├── configure(formatCtx, audioQueue, videoQueue, streamIndices)
│   ├── moveToThread(demuxThread_)
│   └── 连接信号
├── 创建 AudioOutput（如果有音频流）
│   ├── configure(audioQueue, audioDecoder, audioClock)
│   ├── moveToThread(audioThread_)
│   └── 连接信号
├── 创建 VideoDecodeWorker（如果有视频流）
│   ├── configure(videoQueue, videoDecoder, videoFrameQueue)
│   ├── moveToThread(videoThread_)
│   └── 连接信号
├── 启动线程：audio → video → demux
└── pipelineRunning_ = true
```

**注意**：
- 不设置 seekOffset，不设置 startPaused
- 音频时钟由解码帧的 PTS 驱动，不使用字节计数

---

## 四、音频输出流程

```
AudioOutput::start()
├── setupResampler()
├── 创建 QAudioSink
└── while !aborted_:
    ├── if paused_: sleep(50ms), continue
    ├── pkt = queue_->pop()         // 阻塞等待
    ├── if !pkt: break              // EOF 或 abort
    ├── frame = decoder_->decode(pkt)
    ├── swr_convert (重采样)
    ├── ioDevice_->write (播放)
    ├── 更新时钟：
    │   ├── 从 frame->best_effort_timestamp 获取 PTS
    │   ├── 乘以 timeBase 得到秒数
    │   └── clock_->update(ptsSec)
    └── emit positionUpdated(ptsSec)
```

**关键点**：
- 时钟基于实际解码帧的 PTS，不是字节计数
- 这样无论从哪里开始播放，时钟都是正确的

---

## 五、视频同步流程

```
onSyncTimerTick() [每 16ms]
├── clockPos = audioClock_.positionSec()
├── while videoFrameQueue 非空：
│   ├── frame = peek()
│   ├── decision = scheduler.evaluate(frame.ptsSec, clockPos)
│   ├── Drop: frame.ptsSec + 50ms < clockPos → 丢弃
│   ├── Present: frame.ptsSec ≤ clockPos + 50ms → 显示
│   └── Wait: frame.ptsSec > clockPos → 等待
└── emit positionUpdated(clockPos)
```

---

## 六、EOF 处理流程

```
DemuxWorker 到达 EOF
├── audioQueue_->abort()            // 通知音频队列
├── videoQueue_->abort()            // 通知视频队列
└── emit finished()

AudioOutput pop() 返回 null
├── drain 解码器（获取剩余缓冲帧）
├── cleanup()
└── emit finished()

PlayerEngine 收到 AudioOutput::finished
├── if !pipelineRunning_: return    // 忽略旧信号
├── pipelineRunning_ = false
├── stopPipeline()
├── flushQueues()
├── state_ = Idle
└── emit stateChanged()
```

---

## 七、状态机

```
         open()           play()
Idle ──────────→ Idle ──────────→ Playing
  ↑                  ↑                  │
  │                  │    pause()        │
  │                  ├───────────────────┤
  │                  │                   │
  │         seek()   │    seek()         │
  │         ┌────────┤                   │
  │         ↓        │                   │
  │       Paused ←───┘                   │
  │         │                            │
  │         │ play()                     │ EOF
  │         └──────────→ Playing         │
  │                                      │
  └──────────────────────────────────────┘
```

**状态转换规则**：
- `Idle + play() → Playing`（首次播放或 EOF 后重播）
- `Playing + pause() → Paused`
- `Playing + seek() → Paused`
- `Paused + play() → Playing`
- `Paused + seek() → Paused`
- `Playing + EOF → Idle`
- `任何状态 + stop() → Idle`

---

## 八、进度条绑定

```qml
Slider {
    value: seeking ? seekTarget : progressValue
}

// progressValue = playerController.positionMs
// positionMs = audioClock.positionSec() * 1000
// audioClock 由解码帧的 PTS 更新
```

**关键点**：
- seek 期间（seeking=true）：进度条显示 seek 目标
- 播放期间（seeking=false）：进度条显示实际 PTS
- 当实际 PTS 接近 seek 目标时，seeking 自动清除

---

## 九、需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `AudioOutput.cpp` | 时钟更新改为使用 frame PTS |
| `AudioOutput.h` | 移除 seekOffset、startPaused、freezeClock 相关代码 |
| `VideoDecodeWorker.cpp` | 添加 drain 逻辑 |
| `PlayerEngine.cpp` | 简化 seek/play/startPipeline，移除 seekOffset 逻辑 |
| `PlayerEngine.h` | 清理不需要的成员变量 |
| `PlaybackControlBar.qml` | 简化 seeking 逻辑 |

---

## 十、测试场景

1. **首次播放**：打开文件 → 点击播放 → 从头播放
2. **暂停/恢复**：播放 → 暂停 → 播放 → 从暂停位置继续
3. **seek 后播放**：播放 → seek 到中间 → 播放 → 从关键帧开始播放
4. **seek 后暂停再播放**：seek → 暂停 → 播放 → 从关键帧开始播放
5. **播放结束重播**：播放到结束 → 点击播放 → 从头播放
6. **多次 seek**：多次拖动进度条 → 播放 → 从最后 seek 的关键帧开始
