#include "controller/PoolChatController.h"

#include <QFile>
#include <QFileDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QBuffer>
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QUrl>

#include <algorithm>

#include <QUuid>

#include <QStandardPaths>

#include "controller/AuthController.h"
#include "infrastructure/Logger.h"

namespace {

// 服务端按 unicode 标量计数（Rust 的 chars().count()），QString::length() 数的是
// UTF-16 码元：一个 emoji 在这里是 2、在那边是 1。代理对只数一次才对得上。
qsizetype codePointCount(const QString &text) {
    qsizetype count = 0;
    for (const QChar ch : text) {
        if (!ch.isLowSurrogate())
            ++count;
    }
    return count;
}

QByteArray jsonBody(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString encoded(const QString &value) {
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

QString stringOf(const QVariantMap &map, const QString &key) {
    const QVariant value = map.value(key);
    // 编号既可能给字符串也可能给整数；整数值一律按整数打出来 ——
    // 18 位雪花号一旦被当成 double 走 toString 就成了 3.18e+17，回读不了。
    if (value.typeId() == QMetaType::Double)
        return QString::number(value.toLongLong());
    return value.toString();
}

} // namespace

PoolChatController::PoolChatController(AuthController *auth, QObject *parent)
    : QObject(parent), auth_(auth) {
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &PoolChatController::pollChatTick);
    // 红点这条通道不看当前页在不在聊天标签：它只拿两个数字，登录着就一直跟。
    // 没登录时 refreshUnread 直接回来，一个请求都不发。
    unreadTimer_.setInterval(kUnreadPollMs);
    connect(&unreadTimer_, &QTimer::timeout, this, &PoolChatController::refreshUnread);
    unreadTimer_.start();
    if (auth_) {
        connect(auth_, &AuthController::loginStatusChanged, this, [this]() {
            if (auth_->sessionToken().isEmpty()) {
                // 退了登录就清零：下一个账号看到红点之前，不该留着上一个账号的数字。
                unreadGroup_ = 0;
                unreadDm_ = 0;
                emit unreadChanged();
                return;
            }
            refreshUnread();
        });
    }
    // 剪贴板一变就重算，QML 那边的 Shortcut.enabled 才会跟着起落。
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
        const bool had = clipboardHasImage_;
        clipboardHasImage_ = clipboardHasImage();
        if (had != clipboardHasImage_)
            emit clipboardImageChanged();
    });
    clipboardHasImage_ = clipboardHasImage();

    if (auth_) {
        // 我的 id 要晚一步才从 /api/auth/me 回来；到齐后把已展示消息的"是我发的"按同一规则
        // 重算一遍，免得删除入口因为早先那次没标上就永久消失。
        connect(auth_, &AuthController::userInfoChanged, this, [this]() {
            emit myUserIdChanged();
            for (QVariant &value : messages_)
                value = toStamp(QJsonValue::fromVariant(value));
            if (!messages_.isEmpty())
                emit groupChanged();
            for (QVariant &value : threadMessages_)
                value = toStamp(QJsonValue::fromVariant(value));
            if (!threadMessages_.isEmpty())
                emit threadChanged();
        });
    }
}

QString PoolChatController::myUserId() const {
    return auth_ ? auth_->userId().trimmed() : QString();
}

qint64 PoolChatController::elapsedSince(const QElapsedTimer &timer) const {
    return timer.isValid() ? timer.elapsed() : kMinSendIntervalMs + 1;
}

void PoolChatController::send(const QString &path, const QByteArray &body, const QByteArray &verb,
                              Callback cb, const QByteArray &contentType, int timeoutMs) {
    const QString token = auth_ ? auth_->sessionToken() : QString();
    if (token.isEmpty()) {
        cb(PoolApi::Result{false, QJsonValue(), QStringLiteral("请先登录")});
        return;
    }
    // 出站与"连接类失败补发一次"都在 PoolApi::sendRequest 里，和「我的」页同一通道。
    PoolApi::sendRequest(this, nam_, auth_->serverUrl(), path, body, verb, token, cb,
                         0, contentType, timeoutMs);
}

QString PoolChatController::sendBlockReason(const QString &content, qint64 elapsedMs) {
    return blockReasonFor(content, elapsedMs, false);
}

QString PoolChatController::blockReasonFor(const QString &content, qint64 elapsedMs,
                                           bool hasAttachment) {
    const QString text = content.trimmed();
    // 措辞跟 chat_send 一致：内容、图片和动漫至少填一项。挂了卡片就按"有内容"放行。
    if (text.isEmpty() && !hasAttachment)
        return QStringLiteral("消息内容、图片和动漫至少填一项");
    if (codePointCount(text) > kMaxContentLength)
        return QStringLiteral("消息过长（最多 %1 字）").arg(kMaxContentLength);
    if (elapsedMs < kMinSendIntervalMs)
        return QStringLiteral("发送太快，请稍后再试");
    return QString();
}

QJsonArray PoolChatController::mentionedUserIds(const QString &content, const QVariantList &users) {
    QList<QPair<QString, QString>> candidates; // 用户名 -> id
    for (const QVariant &value : users) {
        const QVariantMap map = value.toMap();
        const QString name = map.value(QStringLiteral("username")).toString().trimmed();
        const QString id = map.value(QStringLiteral("id")).toString().trimmed();
        if (name.isEmpty() || id.isEmpty())
            continue;
        candidates.append(qMakePair(name, id));
    }
    // 先长后短：命中 "@李明" 后把这段从正文里划掉，"@李" 就不会再吃一次。
    std::sort(candidates.begin(), candidates.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first.length() > rhs.first.length();
    });

    QSet<QString> seen;
    QJsonArray result;
    QString working = content;
    for (const auto &candidate : candidates) {
        if (result.size() >= 10) // 服务端最多收 10 个
            break;
        const QString token = QLatin1Char('@') + candidate.first;
        if (!working.contains(token, Qt::CaseSensitive))
            continue;
        working.replace(token, QStringLiteral(" "), Qt::CaseSensitive);
        if (seen.contains(candidate.second))
            continue;
        seen.insert(candidate.second);
        QJsonObject mention;
        mention.insert(QStringLiteral("id"), candidate.second);
        result.append(mention);
    }
    return result;
}

QString PoolChatController::groupBlockReason(const QString &content) const {
    if (imageUploading_)
        return QStringLiteral("图片还在上传，稍等一下");
    return blockReasonFor(content, elapsedSince(groupSendClock_), hasAttachment());
}

QString PoolChatController::threadBlockReason(const QString &content) const {
    if (threadPeerId_.isEmpty())
        return QStringLiteral("先选一个会话");
    if (imageUploading_)
        return QStringLiteral("图片还在上传，稍等一下");
    return blockReasonFor(content, elapsedSince(dmSendClock_), hasAttachment());
}

bool PoolChatController::hasAttachment() const {
    return !pendingAnimeId_.isEmpty() || !pendingImages_.isEmpty();
}

void PoolChatController::clearPendingImages() {
    if (pendingImages_.isEmpty() && imageMessage_.isEmpty())
        return;
    pendingImages_.clear();
    imageMessage_.clear();
    emit pendingImagesChanged();
}

void PoolChatController::removePendingImage(int index) {
    if (index < 0 || index >= pendingImages_.size())
        return;
    pendingImages_.removeAt(index);
    emit pendingImagesChanged();
}

PoolChatController::MultipartBody PoolChatController::buildImagePart(const QString &fileName,
                                                                    const QByteArray &bytes) {
    const QByteArray boundary = QByteArrayLiteral("MeloBox")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    // 文件名里的引号会把 form-data 那一行拆坏，先剥掉；路径不带上去。
    const QString safeName = QString(QFileInfo(fileName).fileName()).remove(QLatin1Char('"'));
    QByteArray body;
    body += QByteArrayLiteral("--") + boundary + "\r\n";
    body += QByteArrayLiteral("Content-Disposition: form-data; name=\"file\"; filename=\"")
        + safeName.toUtf8() + "\"\r\n";
    // 服务端不看这个头，按魔数认类型（PNG/JPG/GIF/BMP/WEBP）。
    body += QByteArrayLiteral("Content-Type: application/octet-stream\r\n");
    body += "\r\n";
    body += bytes;
    body += "\r\n--" + boundary + "--\r\n";
    MultipartBody out;
    out.contentType = QByteArrayLiteral("multipart/form-data; boundary=") + boundary;
    out.body = body;
    return out;
}

void PoolChatController::chooseAndUploadImages() {
    if (imageUploading_)
        return;
    const int room = kMaxImagesPerMessage - pendingImages_.size();
    if (room <= 0) {
        imageMessage_ = QStringLiteral("一条消息最多 %1 张图").arg(kMaxImagesPerMessage);
        emit pendingImagesChanged();
        return;
    }
    // 原生 Win32 选择框：这个包没带 QtQuick.Dialogs，走 QML 对话框会当场找不到模块。
    const QStringList picked = QFileDialog::getOpenFileNames(
        nullptr, QStringLiteral("选择图片"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
    if (picked.isEmpty())
        return;
    imageMessage_ = picked.size() > room
        ? QStringLiteral("一条消息最多 %1 张，只排进前 %2 张")
              .arg(kMaxImagesPerMessage).arg(room)
        : QString();
    imageQueue_.clear();
    enqueuePaths(picked, room);
    imageQueueTotal_ = imageQueue_.size();
    uploadNextQueuedImage();
}

void PoolChatController::enqueuePaths(const QStringList &paths, int room) {
    for (const QString &path : paths) {
        if (imageQueue_.size() >= room)
            break;
        const QFileInfo info(path);
        QFile file(path);
        if (!info.exists() || !file.open(QIODevice::ReadOnly)) {
            imageMessage_ = QStringLiteral("读不到这个文件：%1").arg(info.fileName());
            continue;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        // 这三句措辞跟 upload.rs 一字不差，别让用户在同一条规则下看到两种说法。
        if (bytes.isEmpty()) {
            imageMessage_ = QStringLiteral("图片内容为空");
            continue;
        }
        if (bytes.size() > kMaxImageBytes) {
            imageMessage_ = QStringLiteral("图片过大（单张最多 5MB）");
            continue;
        }
        ImageJob job;
        job.name = info.fileName();
        job.bytes = bytes;
        imageQueue_.append(job);
    }
}

// 截图直接转 PNG 动辄几 MB，还要被服务端中继到图床；长边压到 1600 再编，
// 聊天里的图本来就是给人看一眼的。
constexpr int kPasteMaxEdge = 1600;

QByteArray PoolChatController::pngBytes(const QImage &source) {
    if (source.isNull())
        return {};
    QImage image = source;
    const int longest = std::max(image.width(), image.height());
    if (longest > kPasteMaxEdge) {
        const qreal scale = qreal(kPasteMaxEdge) / qreal(longest);
        image = image.scaled(QSize(qRound(image.width() * scale), qRound(image.height() * scale)),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return bytes;
}

bool PoolChatController::clipboardHasImage() const {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data)
        return false;
    if (data->hasImage())
        return true;
    // 从资源管理器复制一个图片文件也算：那种剪贴板里只有 url，没有位图。
    for (const QUrl &url : data->urls()) {
        if (isImageFile(localPathOf(url)))
            return true;
    }
    return false;
}

bool PoolChatController::isImageFile(const QString &path) {
    static const QStringList suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp")
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

QString PoolChatController::localPathOf(const QUrl &url) {
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

void PoolChatController::pasteImageFromClipboard() {
    if (imageUploading_)
        return;
    const int room = kMaxImagesPerMessage - pendingImages_.size();
    if (room <= 0) {
        imageMessage_ = QStringLiteral("一条消息最多 %1 张图").arg(kMaxImagesPerMessage);
        emit pendingImagesChanged();
        return;
    }
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data)
        return;

    // 两种来源：剪贴板里的位图（截图、从网页/聊天软件复制图片），
    // 或者剪贴板里的图片文件（在资源管理器里 Ctrl+C 一个文件）。
    QStringList files;
    for (const QUrl &url : data->urls()) {
        const QString path = localPathOf(url);
        if (isImageFile(path))
            files << path;
    }

    if (data->hasImage()) {
        const QImage image = qvariant_cast<QImage>(data->imageData());
        Logger::instance().info(QStringLiteral("Ctrl+V 剪贴板位图 %1x%2")
                                    .arg(image.width()).arg(image.height()));
        if (image.isNull())
            return;
        const QByteArray bytes = pngBytes(image);
        if (bytes.isEmpty()) {
            imageMessage_ = QStringLiteral("剪贴板的图片转不成 PNG");
            emit pendingImagesChanged();
            return;
        }
        if (bytes.size() > kMaxImageBytes) {
            imageMessage_ = QStringLiteral("图片过大（单张最多 5MB）");
            emit pendingImagesChanged();
            return;
        }
        ImageJob job;
        job.name = QStringLiteral("paste-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        job.bytes = bytes;
        imageQueue_.clear();
        imageQueue_.append(job);
        imageQueueTotal_ = 1;
        imageMessage_.clear();
        uploadNextQueuedImage();
        return;
    }

    if (!files.isEmpty()) {
        Logger::instance().info(QStringLiteral("Ctrl+V 剪贴板图片文件 %1 个").arg(files.size()));
        imageQueue_.clear();
        enqueuePaths(files, room);
        imageQueueTotal_ = imageQueue_.size();
        if (imageQueue_.isEmpty()) {
            // 限额/读不到之类的具体原因由 enqueuePaths 留在 imageMessage_ 里。
            emit pendingImagesChanged();
            return;
        }
        uploadNextQueuedImage();
        return;
    }

    // 走到这里说明这次 Ctrl+V 没有可用的图片：不出声，让 TextInput 自己粘贴文本。
    Logger::instance().info(QStringLiteral("Ctrl+V 剪贴板没有图片，格式: %1")
                                .arg(QStringList(data->formats().mid(0, 5)).join(QStringLiteral(", "))));
}

void PoolChatController::uploadNextQueuedImage() {
    if (imageQueue_.isEmpty()) {
        imageUploading_ = false;
        emit pendingImagesChanged();
        return;
    }
    // 读文件与限额都在入队前做掉了（选文件 and 剪贴板共用一条路），这里只管发。
    const ImageJob job = imageQueue_.takeFirst();
    imageUploading_ = true;
    const QString name = job.name;
    const QByteArray bytes = job.bytes;

    const int done = imageQueueTotal_ - imageQueue_.size();
    imageMessage_ = QStringLiteral("上传中 %1/%2…").arg(done).arg(imageQueueTotal_);
    emit pendingImagesChanged();

    const MultipartBody part = buildImagePart(name, bytes);
    const int uploadBytes = bytes.size();
    QElapsedTimer clock;
    clock.start();
    // 60 秒传输额度：图床中继慢的时候 15 秒会误判成失败。
    send(QStringLiteral("/api/upload/image"), part.body, "POST",
         [this, name, uploadBytes, clock](const PoolApi::Result &result) {
             Logger::instance().info(QStringLiteral("图片上传回包 %1 (%2 字节, %3ms): %4")
                 .arg(name).arg(uploadBytes).arg(clock.elapsed())
                 .arg(result.ok ? QStringLiteral("成功") : result.message));
             if (result.ok) {
                 const QString url = result.data.toObject()
                                         .value(QStringLiteral("url")).toString();
                 if (url.isEmpty()) {
                     imageMessage_ = QStringLiteral("图床没回地址");
                 } else {
                     pendingImages_ << url;
                     imageMessage_ = QString();
                 }
             } else {
                 // 服务端配额/图床失败都带中文原因，原样显示，别再排后面的图。
                 imageMessage_ = result.message;
                 imageQueue_.clear();
             }
             uploadNextQueuedImage();
         }, part.contentType);
}

QJsonObject PoolChatController::pendingAnimeBody() const {
    // name 是服务端的硬要求（parse_anime 空片名直接 400），只有图没卡片就别塞。
    if (pendingAnimeName_.trimmed().isEmpty())
        return {};
    QJsonObject anime;
    anime.insert(QStringLiteral("name"), pendingAnimeName_);
    anime.insert(QStringLiteral("pic"), pendingAnimePic_);
    anime.insert(QStringLiteral("id"), pendingAnimeId_);
    return anime;
}

void PoolChatController::clearPendingAnime() {
    if (pendingAnimeName_.isEmpty() && pendingAnimePic_.isEmpty() && pendingAnimeId_.isEmpty())
        return;
    pendingAnimeName_.clear();
    pendingAnimePic_.clear();
    pendingAnimeId_.clear();
    emit pendingAnimeChanged();
}

QByteArray PoolChatController::groupBodyFor(const QString &content) const {
    QJsonObject body;
    body.insert(QStringLiteral("content"), content.trimmed());
    const QJsonObject anime = pendingAnimeBody();
    if (!anime.isEmpty())
        body.insert(QStringLiteral("anime"), anime);
    if (!pendingImages_.isEmpty()) {
        QJsonArray images;
        for (const QString &url : pendingImages_)
            images.append(url);
        body.insert(QStringLiteral("images"), images);
    }
    const QJsonArray mentions = mentionedUserIds(content, users_);
    if (!mentions.isEmpty())
        body.insert(QStringLiteral("mentions"), mentions);
    return jsonBody(body);
}

QVariantMap PoolChatController::toStamp(QJsonValue item) const {
    QVariantMap map = item.toVariant().toMap();
    const QString mine = myUserId();
    map.insert(QStringLiteral("mine"),
               !mine.isEmpty()
                   && (stringOf(map, QStringLiteral("user_id")) == mine
                       || stringOf(map, QStringLiteral("sender_id")) == mine));
    return map;
}

void PoolChatController::removeMessageById(QVariantList &list, const QString &messageId) const {
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).toMap().value(QStringLiteral("id")).toString() == messageId)
            list.removeAt(i);
    }
}

void PoolChatController::setActive(bool active) {
    if (active_ == active)
        return;
    active_ = active;
    emit activeChanged();

    // 只开关定时器：首屏取数归视图管（MyView 进标签/进页面都会 reload），
    // 这里再顺手拉一次的话，一次进页会发两趟全量请求。
    if (active_)
        pollTimer_.start(kPollIntervalMs);
    else {
        pollTimer_.stop();
        // 离开的这一刻才是"看到了"的终点：进度只推到这里渲染过的最后一条。
        markGroupRead();
    }
}

void PoolChatController::setViewMode(int mode) {
    // 只是记下"轮询下一轮该喂哪个视图"，不在这儿顺手取数：
    // 进视图的取数归 reload() 管，这里再拉一次就成了一轮两趟请求。
    const int next = mode == 1 || mode == 2 ? mode : 0;
    if (next == viewMode_)
        return;
    // 从群聊切走 = 这一屏已经看完了，进度推到这里渲染过的最后一条。
    if (viewMode_ == 0)
        markGroupRead();
    viewMode_ = next;
    tickCount_ = 0;
}

void PoolChatController::markGroupRead() {
    const QString at = groupCursor_;
    if (at.isEmpty() || !auth_ || auth_->sessionToken().isEmpty())
        return;
    QJsonObject body;
    body.insert(QStringLiteral("at"), at);
    send(QStringLiteral("/api/chat/read"), QJsonDocument(body).toJson(QJsonDocument::Compact), "POST",
         [this, at](const PoolApi::Result &result) {
             if (!result.ok) {
                 Logger::instance().warn(QStringLiteral("群聊已读进度上报失败 (%1): %2")
                     .arg(at, result.message));
                 return;
             }
             // 进度挪成功了就把红点的数取回来，不然要等 30 秒那一轮才熄。
             refreshUnread();
         });
}

void PoolChatController::refreshUnread() {
    if (!auth_ || auth_->sessionToken().isEmpty() || unreadInFlight_)
        return;
    unreadInFlight_ = true;
    send(QStringLiteral("/api/chat/unread"), {}, "GET", [this](const PoolApi::Result &result) {
        unreadInFlight_ = false;
        if (!result.ok) {
            // 后台通道拉不到就保留上一次显示的数字，别把红点刷成"没有未读"。
            // 只在全好 → 第一次坏时写一行：每 30 秒一条会把日志刷没的，一次故障留一条才看得见。
            if (!unreadWarned_) {
                unreadWarned_ = true;
                Logger::instance().warn(QStringLiteral("未读计数拉取失败: %1").arg(result.message));
            }
            return;
        }
        unreadWarned_ = false;
        const QPair<int, int> lights = unreadPairOf(result.data.toObject(),
                                                    active_ && viewMode_ == 0);
        const bool changed = lights.first != unreadGroup_ || lights.second != unreadDm_;
        // 第一次拉到就写一行（哪怕两个数都是 0）：否则"红点为什么不亮"和"根本没拉过"
        // 在日志里长得一样 —— 这是发图那条"一直卡在上传中"教过的。
        if (changed || !unreadLoggedOnce_) {
            unreadLoggedOnce_ = true;
            Logger::instance().info(QStringLiteral("未读计数: 群聊 %1 / 私信 %2")
                .arg(lights.first).arg(lights.second));
        }
        if (!changed)
            return;
        unreadGroup_ = lights.first;
        unreadDm_ = lights.second;
        emit unreadChanged();
    });
}

QPair<int, int> PoolChatController::unreadPairOf(const QJsonObject &data, bool viewingGroup) {
    // 键缺失或不是数字都当 0：宁可不亮红点，也不亮一个凭空的数。
    const int group = qMax(0, data.value(QStringLiteral("group")).toInt());
    const int dm = qMax(0, data.value(QStringLiteral("dm")).toInt());
    // 正在看群聊那一屏 = 屏幕上的都在读，那颗不该亮着（读进度还是离开时才交回服务端）。
    return {viewingGroup ? 0 : group, dm};
}

void PoolChatController::searchAnime(const QString &keyword) {
    const QString text = keyword.trimmed();
    if (text.isEmpty()) {
        if (!animeResults_.isEmpty()) {
            animeResults_.clear();
            emit animeChanged();
        }
        return;
    }
    // 打字比回包快：只有最后一次请求的回包能盖当前结果。
    // 连打时允许两趟同时在飞，靠序号把过期回包丢掉：
    // 后发那条才是人眼前要的关键词，早回来的直接扔。
    const quint64 serial = ++animeSerial_;
    animeLoading_ = true;
    emit animeChanged();

    const QString path = QStringLiteral("/api/anime/list?keyword=%1&page=1&limit=8")
        .arg(encoded(text));
    send(path, {}, "GET", [this, serial](const PoolApi::Result &result) {
        animeLoading_ = false;
        if (serial != animeSerial_) {
            emit animeChanged();
            return;
        }
        if (!result.ok) {
            animeResults_.clear();
            Logger::instance().warn(QStringLiteral("## 动漫搜索失败: %1").arg(result.message));
            emit animeChanged();
            return;
        }
        animeResults_.clear();
        const QJsonArray items = result.data.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &item : items) {
            const QVariantMap map = item.toVariant().toMap();
            const QString name = stringOf(map, QStringLiteral("name"));
            if (name.isEmpty())
                continue;
            QVariantMap out;
            out.insert(QStringLiteral("name"), name);
            out.insert(QStringLiteral("pic"), stringOf(map, QStringLiteral("pic")));
            // 网页版取 new_id 优先：那是 18 位新号，桌面端也按这个号能直接开详情。
            const QString newId = stringOf(map, QStringLiteral("new_id"));
            out.insert(QStringLiteral("id"),
                       newId.isEmpty() ? stringOf(map, QStringLiteral("id")) : newId);
            animeResults_.append(out);
        }
        emit animeChanged();
    });
}

void PoolChatController::pickAnime(const QString &name, const QString &pic, const QString &id) {
    const QString title = name.trimmed();
    if (title.isEmpty())
        return;
    pendingAnimeName_ = title.left(60);
    pendingAnimePic_ = pic.trimmed();
    pendingAnimeId_ = id.trimmed();
    if (!animeResults_.isEmpty()) {
        animeResults_.clear();
        emit animeChanged();
    }
    emit pendingAnimeChanged();
}

void PoolChatController::clearAnime() {
    clearPendingAnime();
}

void PoolChatController::pollChatTick() {
    if (!active_) {
        pollTimer_.stop();
        return;
    }
    ++tickCount_;
    // 一个定时器只喂当前视图：在看私信就不去拉群聊，回到群聊那一轮会用旧游标把落下的补回来。
    if (viewMode_ == 2) {
        fetchThread(threadCursor_, false);
        return;
    }
    if (viewMode_ == 1) {
        if (tickCount_ % kThreadsPollEveryTicks == 0)
            pollThreadsQuietly();
        return;
    }
    if (groupFetchInFlight_)
        return; // 上一轮还没回来就跳过这一轮，不叠请求
    // 一条消息都还没拉到过就没有游标可用，只能全量补一次。
    fetchGroup(groupCursor_, false);
}

void PoolChatController::refreshGroup() {
    if (groupFetchInFlight_) {
        // 刷新点在请求飞行中就排队，回包落地后立刻补一次全量：
        // 直接丢掉这一下点击的话，用户会以为按钮坏了。
        groupFullRefreshQueued_ = true;
        return;
    }
    fetchGroup(QString(), true);
}

void PoolChatController::fetchGroup(const QString &after, bool showLoading) {
    if (groupFetchInFlight_)
        return;
    groupFetchInFlight_ = true;
    if (showLoading && !groupLoading_) {
        groupLoading_ = true;
        emit groupChanged();
    }

    QString path = QStringLiteral("/api/chat/messages");
    if (!after.isEmpty())
        path += QStringLiteral("?after=%1").arg(encoded(after));

    send(path, {}, "GET", [this, after, showLoading](const PoolApi::Result &result) {
        groupFetchInFlight_ = false;
        if (showLoading)
            groupLoading_ = false;

        if (result.ok) {
            if (after.isEmpty()) {
                messages_.clear();
                seenGroupIds_.clear();
            }
            groupMessage_.clear();
            const QJsonObject root = result.data.toObject();
            // 未读线只认全量那一趟：增量轮询的回包里 first_unread_id 是空的，
            // 照抄过去会让分隔线在五秒后从人眼皮底下消失。未读的"数字"归 /api/chat/unread 管。
            if (after.isEmpty())
                groupFirstUnreadId_ = root.value(QStringLiteral("first_unread_id")).toString();
            appendGroupMessages(root.value(QStringLiteral("items")).toArray());
        } else {
            // 只换状态行：已经显示在屏幕上的消息一条都不清，拉回来就还在。
            groupMessage_ = result.message;
            if (!after.isEmpty())
                Logger::instance().warn(QStringLiteral("群聊增量拉取失败: %1").arg(result.message));
        }
        emit groupChanged();

        if (groupFullRefreshQueued_) {
            groupFullRefreshQueued_ = false;
            refreshGroup();
        }
    });
}

void PoolChatController::appendGroupMessages(const QJsonArray &items) {
    QList<QVariantMap> stamps;
    stamps.reserve(items.size());
    for (const QJsonValue &item : items)
        stamps.append(toStamp(item));
    appendUnseenStamps(messages_, seenGroupIds_, groupCursor_, stamps, kMaxKeptMessages);
}

bool PoolChatController::appendUnseenStamps(QVariantList &list, QSet<QString> &seenIds,
                                             QString &cursor, const QList<QVariantMap> &stamps,
                                             qsizetype maxKept) {
    bool appended = false;
    for (const QVariantMap &message : stamps) {
        const QString id = message.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && seenIds.contains(id))
            continue;
        if (!id.isEmpty())
            seenIds.insert(id);
        list.append(message);
        appended = true;

        const QString created = message.value(QStringLiteral("created_at")).toString();
        if (created > cursor)
            cursor = created; // 同一格式（RFC3339 UTC）下字典序就是时间序
    }

    if (list.size() > maxKept) {
        const qsizetype drop = list.size() - maxKept;
        for (qsizetype i = 0; i < drop; ++i)
            seenIds.remove(list.at(i).toMap().value(QStringLiteral("id")).toString());
        list = list.mid(drop);
    }
    return appended;
}

void PoolChatController::sendGroupMessage(const QString &content) {
    const QString block = groupBlockReason(content);
    if (!block.isEmpty()) {
        groupMessage_ = block;
        emit groupChanged();
        return;
    }

    send(QStringLiteral("/api/chat/messages"), groupBodyFor(content), "POST",
         [this](const PoolApi::Result &result) {
             if (!result.ok) {
                 groupMessage_ = result.message;
                 Logger::instance().warn(QStringLiteral("群聊发送失败: %1").arg(result.message));
                 emit groupChanged();
                 return;
             }
             groupSendClock_.restart();
             groupMessage_.clear();
             clearPendingAnime();
             clearPendingImages();
             QJsonArray created;
             created.append(result.data);
             appendGroupMessages(created);
             Logger::instance().info(QStringLiteral("群聊已发送 %1 字").arg(
                 codePointCount(result.data.toObject().value(QStringLiteral("content")).toString())));
             emit groupChanged();
         });
}

void PoolChatController::deleteGroupMessage(const QString &messageId) {
    const QString id = messageId.trimmed();
    if (id.isEmpty()) {
        groupMessage_ = QStringLiteral("这条消息没有可用编号，删不掉");
        emit groupChanged();
        return;
    }
    send(QStringLiteral("/api/chat/%1").arg(encoded(id)), {}, "DELETE",
         [this, id](const PoolApi::Result &result) {
             if (!result.ok) {
                 groupMessage_ = result.message;
                 Logger::instance().warn(QStringLiteral("群聊删除失败: %1").arg(result.message));
                 emit groupChanged();
                 return;
             }
             removeMessageById(messages_, id);
             groupMessage_.clear();
             Logger::instance().info(QStringLiteral("群聊消息已删除: %1").arg(id));
             emit groupChanged();
         });
}

void PoolChatController::searchUsers(const QString &keyword) {
    QString path = QStringLiteral("/api/chat/users");
    const QString text = keyword.trimmed();
    if (!text.isEmpty())
        path += QStringLiteral("?keyword=%1").arg(encoded(text));

    send(path, {}, "GET", [this, text](const PoolApi::Result &result) {
        if (!result.ok) {
            // @ 候选是辅助功能，失败时保留上一份名单，不占用消息状态行。
            Logger::instance().warn(QStringLiteral("@ 用户搜索失败 (%1): %2")
                                        .arg(text, result.message));
            return;
        }
        users_.clear();
        const QJsonArray items = result.data.toObject().value(QStringLiteral("items")).toArray();
        for (const auto &item : items)
            users_.append(item.toVariant());
        emit usersChanged();
    });
}

void PoolChatController::refreshThreads() {
    if (threadsLoading_)
        return;
    threadsLoading_ = true;
    emit threadsChanged();

    send(QStringLiteral("/api/private/threads"), {}, "GET", [this](const PoolApi::Result &result) {
        threadsLoading_ = false;
        if (!result.ok) {
            threadsMessage_ = result.message;
            emit threadsChanged();
            return;
        }
        threads_.clear();
        const QJsonArray items = result.data.toObject().value(QStringLiteral("items")).toArray();
        for (const auto &item : items)
            threads_.append(item.toVariant());
        threadsMessage_.clear();
        emit threadsChanged();
    });
}

void PoolChatController::pollThreadsQuietly() {
    // 用户点了刷新（threadsLoading_）或上一轮静默拉取还没回来，就跳过这一轮。
    if (threadsLoading_ || threadsPollInFlight_)
        return;
    threadsPollInFlight_ = true;
    send(QStringLiteral("/api/private/threads"), {}, "GET",
         [this](const PoolApi::Result &result) {
             threadsPollInFlight_ = false;
             if (!result.ok) {
                 Logger::instance().warn(QStringLiteral("私信列表静默刷新失败: %1")
                     .arg(result.message));
                 return; // 轮询失败不动状态行，免得一条网络错误每 15 秒盖掉一次用户看到的东西
             }
             QVariantList fresh;
             const QJsonArray items = result.data.toObject().value(QStringLiteral("items")).toArray();
             for (const auto &item : items)
                 fresh.append(item.toVariant());
             if (fresh == threads_)
                 return; // 一模一样就不重建 delegate：hover 高亮不会被轮询抢掉
             threads_ = fresh;
             threadsMessage_.clear();
             emit threadsChanged();
         });
}

void PoolChatController::openThread(const QString &peerId, const QString &peerName) {
    const QString id = peerId.trimmed();
    if (id.isEmpty()) {
        threadMessage_ = QStringLiteral("这条会话没有可用编号，打不开");
        emit threadChanged();
        return;
    }
    threadPeerId_ = id;
    threadPeerName_ = peerName.trimmed();
    threadMessages_.clear();
    threadMessage_.clear();
    seenThreadIds_.clear();
    threadCursor_.clear();
    // 分隔线属于上一会话的话，回包到达前会画错地方，先清掉。
    threadFirstUnreadId_.clear();
    emit threadChanged();
    fetchThread(QString(), true);
}

void PoolChatController::closeThread() {
    threadPeerId_.clear();
    threadPeerName_.clear();
    threadMessages_.clear();
    threadMessage_.clear();
    seenThreadIds_.clear();
    threadCursor_.clear();
    threadFirstUnreadId_.clear();
    emit threadChanged();
}

void PoolChatController::fetchThread(const QString &after, bool showLoading) {
    if (threadPeerId_.isEmpty() || threadFetchInFlight_)
        return;

    const QString peer = threadPeerId_;
    threadFetchInFlight_ = true;
    if (showLoading && !threadLoading_) {
        threadLoading_ = true;
        emit threadChanged();
    }

    // 这个请求有副作用：把对方发来的消息标记成已读，所以本地会话行上的未读数跟着清掉。
    QString path = QStringLiteral("/api/private/messages?with=%1").arg(encoded(peer));
    if (!after.isEmpty())
        path += QStringLiteral("&after=%1").arg(encoded(after));

    send(path, {}, "GET", [this, peer, after, showLoading](const PoolApi::Result &result) {
        threadFetchInFlight_ = false;
        if (showLoading)
            threadLoading_ = false;

        if (peer != threadPeerId_) {
            // 用户在这趟请求飞行中切了会话：这一份回包不能盖当前视图，
            // 但也不能就此丢掉当前会话——就地补拉一次全量，不然要等下一次点击才有内容。
            emit threadChanged();
            fetchThread(QString(), true);
            return;
        }
        if (!result.ok) {
            // 只换状态行：已经显示在屏幕上的消息一条都不清，拉回来就还在。
            threadMessage_ = result.message;
            if (!after.isEmpty())
                Logger::instance().warn(QStringLiteral("私信增量拉取失败: %1").arg(result.message));
            emit threadChanged();
            return;
        }

        const QJsonObject root = result.data.toObject();
        bool changed = false;
        if (after.isEmpty()) {
            threadMessages_.clear();
            seenThreadIds_.clear();
            threadCursor_.clear();
            // 未读线只认全量那一趟：增量回包里的 first_unread_id 早被上一趟标成已读，
            // 照抄过来会让分隔线五秒后从人眼皮底下消失。
            threadFirstUnreadId_ = root.value(QStringLiteral("first_unread_id")).toString();
            changed = true;
            for (int i = 0; i < threads_.size(); ++i) {
                QVariantMap map = threads_.at(i).toMap();
                if (map.value(QStringLiteral("peer")).toString() == peer) {
                    map.insert(QStringLiteral("unread"), 0);
                    threads_[i] = map;
                }
            }
            // 这趟请求本身就有"把对方发来的标成已读"的副作用，顺手把红点的数取回来，
            // 不然私信读了要等 30 秒那一轮才熄。
            refreshUnread();
        }
        if (appendThreadMessages(root.value(QStringLiteral("items")).toArray()))
            changed = true;
        if (changed)
            threadMessage_.clear();
        if (changed || showLoading)
            emit threadChanged();
    });
}

bool PoolChatController::appendThreadMessages(const QJsonArray &items) {
    QList<QVariantMap> stamps;
    stamps.reserve(items.size());
    for (const QJsonValue &item : items)
        stamps.append(toStamp(item));
    return appendUnseenStamps(threadMessages_, seenThreadIds_, threadCursor_, stamps,
                              kMaxKeptMessages);
}

void PoolChatController::sendThreadMessage(const QString &content) {
    if (threadPeerId_.isEmpty()) {
        threadMessage_ = QStringLiteral("先选一个会话");
        emit threadChanged();
        return;
    }
    // 服务端按"每人每对"限 2 秒，这里用一个全局时钟，只会更严格，不会放过 429。
    const QString block = threadBlockReason(content);
    if (!block.isEmpty()) {
        threadMessage_ = block;
        emit threadChanged();
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("to"), threadPeerId_);
    body.insert(QStringLiteral("content"), content.trimmed());
    const QJsonObject anime = pendingAnimeBody();
    if (!anime.isEmpty())
        body.insert(QStringLiteral("anime"), anime);
    if (!pendingImages_.isEmpty()) {
        QJsonArray images;
        for (const QString &url : pendingImages_)
            images.append(url);
        body.insert(QStringLiteral("images"), images);
    }

    const QString peer = threadPeerId_;
    send(QStringLiteral("/api/private/messages"), jsonBody(body), "POST",
         [this, peer](const PoolApi::Result &result) {
             if (!result.ok) {
                 threadMessage_ = result.message;
                 Logger::instance().warn(QStringLiteral("私信发送失败: %1").arg(result.message));
                 emit threadChanged();
                 return;
             }
             dmSendClock_.restart();
             threadMessage_.clear();
             clearPendingAnime();
             clearPendingImages();
             if (peer == threadPeerId_) {
                 // 走 appendThreadMessages：它顺手把 id 记进 seen、把游标推过去，
                 // 下一轮增量拉回同一条才不会变成两条一模一样的气泡。
                 QJsonArray created;
                 created.append(result.data);
                 appendThreadMessages(created);
             }
             Logger::instance().info(QStringLiteral("私信已发送: %1").arg(peer));
             emit threadChanged();
             refreshThreads();
         });
}

void PoolChatController::deleteThreadMessage(const QString &messageId) {
    const QString id = messageId.trimmed();
    if (id.isEmpty()) {
        threadMessage_ = QStringLiteral("这条消息没有可用编号，删不掉");
        emit threadChanged();
        return;
    }
    send(QStringLiteral("/api/private/%1").arg(encoded(id)), {}, "DELETE",
         [this, id](const PoolApi::Result &result) {
             if (!result.ok) {
                 threadMessage_ = result.message;
                 Logger::instance().warn(QStringLiteral("私信删除失败: %1").arg(result.message));
                 emit threadChanged();
                 return;
             }
             removeMessageById(threadMessages_, id);
             threadMessage_.clear();
             Logger::instance().info(QStringLiteral("私信已删除: %1").arg(id));
             emit threadChanged();
         });
}
