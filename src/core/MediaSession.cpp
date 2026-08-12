#include "core/MediaSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QTimer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QEventLoop>
#include <QRegularExpression>
#include <QUrl>

#include "infrastructure/Logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

namespace {
QByteArray browserUserAgent() {
    return QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36");
}

QString originFromUrl(const QUrl &url) {
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return {};

    QString origin = url.scheme() + QStringLiteral("://") + url.host();
    if (url.port() > 0)
        origin += QStringLiteral(":") + QString::number(url.port());
    return origin;
}

void applyBrowserHeaders(QNetworkRequest &request, const QUrl &url, const QString &referer = QString()) {
    request.setRawHeader("User-Agent", browserUserAgent());
    const QString origin = originFromUrl(url);
    if (!referer.trimmed().isEmpty()) {
        request.setRawHeader("Referer", referer.toUtf8());
    } else if (!origin.isEmpty()) {
        request.setRawHeader("Referer", origin.toUtf8());
    }
    if (!origin.isEmpty()) {
        request.setRawHeader("Origin", origin.toUtf8());
    }
    request.setRawHeader("Accept", "*/*");
}
}

Error MediaSession::open(const QString &path) {
    close();

    // Auto-detect network URLs
    QUrl url(path);
    if (url.scheme().startsWith("http") || url.scheme().startsWith("rtsp") ||
        url.scheme().startsWith("rtmp") || url.scheme().startsWith("udp") ||
        url.scheme().startsWith("tcp")) {
        return openNetwork(path);
    }

    network_ = false;

    if (!QFileInfo::exists(path)) {
        return Error::failure("file does not exist", ErrorCode::FileNotFound);
    }

    // For local m3u8: strip BOM, write clean temp file, open with protocol_whitelist
    QString openPath = path;
    QString lower = path.toLower();
    if (lower.endsWith(".m3u8") || lower.endsWith(".m3u")) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return Error::failure("cannot open m3u8 file", ErrorCode::FileNotFound);
        }
        QByteArray raw = f.readAll();
        f.close();

        // Strip BOM
        if (raw.size() >= 3 &&
            static_cast<unsigned char>(raw[0]) == 0xEF &&
            static_cast<unsigned char>(raw[1]) == 0xBB &&
            static_cast<unsigned char>(raw[2]) == 0xBF) {
            raw = raw.mid(3);
        }

        if (!raw.trimmed().startsWith("#EXTM3U")) {
            return Error::failure("not a valid m3u8 file", ErrorCode::InvalidFormat);
        }

        // Some m3u8 files use spaces instead of newlines as line separators.
        // FFmpeg requires proper newlines. Normalize: insert \n before # and http://
        QString content = QString::fromUtf8(raw);
        content.replace(QRegularExpression("(\\s)#"), "\n#");
        content.replace(QRegularExpression("(\\s)(http://)"), "\n\\2");
        content.replace(QRegularExpression("(\\s)(https://)"), "\n\\2");

        // Check if this is an image-based HLS (probe first segment)
        if (parseImageHls(content)) {
            network_ = true;
            imageHls_ = true;
            Logger::instance().info("Image HLS detected: " +
                QString::number(imageHlsSegments_.size()) + " segments, " +
                QString::number(imageHlsDuration_, 'f', 2) + "s total");
            return Error::success();
        }

        // Not image HLS — proceed with FFmpeg

        // FFmpeg HLS demuxer rejects segment URLs without file extensions.
        // Add /segment.ts path to URLs that only have query parameters (e.g. https://host/?ts=...)
        content.replace(QRegularExpression("(https?://[^/\\s]+)/(\\?)"), "\\1/segment.ts\\2");

        // Filter out non-video segment lines (PNG/JPG image segments etc.)
        {
            QStringList outLines;
            for (const QString &line : content.split('\n')) {
                QString trimmed = line.trimmed();
                if (trimmed.isEmpty()) continue;
                if (!trimmed.startsWith('#') &&
                    (trimmed.endsWith(".png", Qt::CaseInsensitive) ||
                     trimmed.endsWith(".jpg", Qt::CaseInsensitive) ||
                     trimmed.endsWith(".jpeg", Qt::CaseInsensitive) ||
                     trimmed.endsWith(".webp", Qt::CaseInsensitive))) {
                    continue;
                }
                outLines << trimmed;
            }
            content = outLines.join('\n');
        }

        raw = content.toUtf8();

        // Write clean (no-BOM) m3u8 to temp file
        tempM3u8Path_ = QFileInfo(path).absolutePath() + "/.miniplayer_temp.m3u8";
        QFile tempFile(tempM3u8Path_);
        if (tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            tempFile.write(raw);
            tempFile.close();
            openPath = tempM3u8Path_;
            Logger::instance().info("Using temp m3u8: " + tempM3u8Path_);
            // Log first 200 chars of temp file for debugging
            QFile verify(tempM3u8Path_);
            if (verify.open(QIODevice::ReadOnly)) {
                Logger::instance().info("Temp m3u8 content: " + QString::fromUtf8(verify.read(200)));
                verify.close();
            }
        }

        // Set as network since segments are HTTP
        network_ = true;
    }

    AVFormatContext *rawContext = avformat_alloc_context();
    if (!rawContext) {
        return Error::failure("failed to allocate format context", ErrorCode::InvalidFormat);
    }

    AVDictionary *opts = nullptr;

    // For m3u8: allow http/https for segment downloads, allow all extensions
    if (lower.endsWith(".m3u8") || lower.endsWith(".m3u")) {
        av_dict_set(&opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto", 0);
        av_dict_set(&opts, "allowed_extensions", "ALL", 0);
        av_dict_set(&opts, "seg_extensions", "ALL", 0);
        interruptData_.abort = false;
        interruptData_.timeoutMs = 30000;
        interruptData_.lastActivity = std::chrono::steady_clock::now();
        rawContext->interrupt_callback.callback = interruptCallback;
        rawContext->interrupt_callback.opaque = &interruptData_;
    }

    int ret = avformat_open_input(&rawContext, openPath.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(ret, errBuf, sizeof(errBuf));
        Logger::instance().error("avformat_open_input failed: " + QString(errBuf));
        return Error::failure(QString("avformat_open_input failed: %1").arg(errBuf), ErrorCode::InvalidFormat);
    }

    formatContext_.reset(rawContext);

    if (avformat_find_stream_info(formatContext_.get(), nullptr) < 0) {
        close();
        return Error::failure("avformat_find_stream_info failed", ErrorCode::InvalidFormat);
    }

    Logger::instance().info("Media opened successfully, duration=" +
        QString::number(formatContext_->duration / AV_TIME_BASE) + "s, " +
        QString::number(formatContext_->nb_streams) + " streams");
    return Error::success();
}

Error MediaSession::openNetwork(const QString &url) {
    network_ = true;
    interruptData_.abort = false;
    interruptData_.timeoutMs = 15000;
    interruptData_.lastActivity = std::chrono::steady_clock::now();
    tempM3u8Path_.clear();
    Logger::instance().info("Opening network stream directly: " + url);

    // For m3u8 URLs: try downloading the manifest and check for image HLS
    QString lower = url.toLower();
    if (false && (lower.contains(".m3u8") || lower.contains(".m3u"))) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        QNetworkAccessManager nam;
        QUrl m3u8Url(url);
        QNetworkRequest req(m3u8Url);
        applyBrowserHeaders(req, m3u8Url);

        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        sslConfig.setProtocol(QSsl::TlsV1_2);
        req.setSslConfiguration(sslConfig);

        nam.setProxy(QNetworkProxy::NoProxy);

        QNetworkReply *reply = nam.get(req);
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit); // 10s timeout
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            reply->deleteLater();

            if (data.size() > 0) {
                QString content = QString::fromUtf8(data);
                Logger::instance().info("Network m3u8 downloaded: " + QString::number(data.size()) + " bytes");

                // Normalize line separators
                content.replace(QRegularExpression("(\\s)#"), "\n#");
                content.replace(QRegularExpression("(\\s)(http://)"), "\n\\2");
                content.replace(QRegularExpression("(\\s)(https://)"), "\n\\2");

                if (parseImageHls(content)) {
                    imageHls_ = true;
                    Logger::instance().info("Network image HLS detected: " +
                        QString::number(imageHlsSegments_.size()) + " segments");
                    return Error::success();
                }

                // Not image HLS — convert relative URLs to absolute, then write to temp file
                QString baseUrl = m3u8Url.scheme() + "://" + m3u8Url.host();
                if (m3u8Url.port() > 0) baseUrl += ":" + QString::number(m3u8Url.port());
                QString basePath = m3u8Url.path();
                int lastSlash = basePath.lastIndexOf('/');
                if (lastSlash >= 0) {
                    baseUrl += basePath.left(lastSlash + 1);
                }

                // Convert relative segment URLs to absolute
                QStringList lines = content.split('\n');
                QStringList resolvedLines;
                for (const QString &line : lines) {
                    QString trimmed = line.trimmed();
                    if (!trimmed.isEmpty() && !trimmed.startsWith('#') &&
                        !trimmed.startsWith("http://") && !trimmed.startsWith("https://")) {
                        // Relative URL — make it absolute
                        resolvedLines << baseUrl + trimmed;
                        Logger::instance().info("Resolved relative URL: " + baseUrl + trimmed);
                    } else {
                        resolvedLines << trimmed;
                    }
                }
                QByteArray resolvedData = resolvedLines.join('\n').toUtf8();

                tempM3u8Path_ = QDir::tempPath() + "/.miniplayer_temp.m3u8";
                QFile tempFile(tempM3u8Path_);
                if (tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    tempFile.write(resolvedData);
                    tempFile.close();
                }
            }
        } else {
            Logger::instance().warn("Failed to download m3u8: " + reply->errorString());
            reply->deleteLater();
        }
    }

    AVFormatContext *rawContext = avformat_alloc_context();
    if (!rawContext) {
        return Error::failure("failed to allocate format context");
    }

    rawContext->interrupt_callback.callback = interruptCallback;
    rawContext->interrupt_callback.opaque = &interruptData_;

    AVDictionary *opts = nullptr;

    // If we downloaded to temp file, use that instead of the URL
    QString openPath = tempM3u8Path_.isEmpty() ? url : tempM3u8Path_;
    if (!tempM3u8Path_.isEmpty()) {
        // Temp file has http/https URLs for segments — need to allow those protocols
        av_dict_set(&opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto", 0);
    } else {
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
    }
    av_dict_set(&opts, "timeout", "15000000", 0);

    int ret = avformat_open_input(&rawContext, openPath.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(ret, errBuf, sizeof(errBuf));
        return Error::failure(QString("avformat_open_input failed: %1").arg(errBuf));
    }

    formatContext_.reset(rawContext);
    interruptData_.lastActivity = std::chrono::steady_clock::now();

    if (avformat_find_stream_info(formatContext_.get(), nullptr) < 0) {
        close();
        return Error::failure("avformat_find_stream_info failed");
    }

    return Error::success();
}

int MediaSession::interruptCallback(void *ctx) {
    auto *data = static_cast<InterruptData *>(ctx);
    if (data->abort.load()) return 1;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - data->lastActivity).count();
    if (elapsed > data->timeoutMs.load()) return 1;

    return 0;
}

void MediaSession::abortIo() {
    interruptData_.abort = true;
}

void MediaSession::resumeIo() {
    interruptData_.abort = false;
    interruptData_.lastActivity = std::chrono::steady_clock::now();
}

void MediaSession::close() {
    interruptData_.abort = true;
    formatContext_.reset();
    network_ = false;
    imageHls_ = false;
    imageHlsSegments_.clear();
    imageHlsDuration_ = 0.0;
    if (!tempM3u8Path_.isEmpty()) {
        QFile::remove(tempM3u8Path_);
        tempM3u8Path_.clear();
    }
}

bool MediaSession::parseImageHls(const QString &content) {
    // Parse m3u8 into segments, then probe the first segment to check if it's an image.
    // This handles URLs without image extensions (e.g., https://host/?ts=xxx returning PNG data).
    std::vector<ImageHlsSegment> segments;
    double currentPts = 0.0;
    double pendingDuration = 0.0;
    bool hasDuration = false;

    for (const QString &line : content.split('\n')) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        if (trimmed.startsWith("#EXTINF:")) {
            QString durStr = trimmed.mid(8);
            int commaIdx = durStr.indexOf(',');
            if (commaIdx >= 0) durStr = durStr.left(commaIdx);
            pendingDuration = durStr.toDouble();
            hasDuration = true;
        } else if (!trimmed.startsWith('#')) {
            ImageHlsSegment seg;
            seg.url = trimmed;
            seg.durationSec = hasDuration ? pendingDuration : 1.0;
            seg.startPtsSec = currentPts;
            segments.push_back(std::move(seg));
            currentPts += seg.durationSec;
            hasDuration = false;
            pendingDuration = 0.0;
        }
    }

    Logger::instance().info("parseImageHls: parsed " + QString::number(segments.size()) + " segments");

    if (segments.empty()) {
        Logger::instance().info("parseImageHls: no segments found");
        return false;
    }

    // Probe the first segment: download and check if it's image data
    // NOTE: Do NOT modify segment URLs - the server may return different content
    // for modified URLs (e.g. fffan.wki8.com/?ts=... returns real images,
    // but fffan.wki8.com/segment.ts?ts=... returns 1x1 placeholders via 302).
    QUrl firstUrl(segments[0].url);
    Logger::instance().info("parseImageHls: probing " + firstUrl.toString().left(120));
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest probeReq(firstUrl);
    applyBrowserHeaders(probeReq, firstUrl);
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConfig.setProtocol(QSsl::TlsV1_2);
    probeReq.setSslConfiguration(sslConfig);

    QNetworkReply *reply = nam.get(probeReq);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
        reply->ignoreSslErrors();
    });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();

    bool isImage = false;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        Logger::instance().info("parseImageHls: downloaded " + QString::number(data.size()) + " bytes, status=" + QString::number(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        if (data.size() < 1000) {
            // Hex dump for debugging
            QString hex;
            for (int i = 0; i < qMin(data.size(), 64); ++i) {
                hex += QString::number(static_cast<unsigned char>(data[i]), 16).rightJustified(2, '0') + " ";
            }
            Logger::instance().info("parseImageHls: response hex: " + hex);
            Logger::instance().info("parseImageHls: response text: " + QString::fromUtf8(data));
        }
        if (data.size() >= 4) {
            Logger::instance().info("parseImageHls: magic bytes: "
                + QString::number(static_cast<unsigned char>(data[0]), 16) + " "
                + QString::number(static_cast<unsigned char>(data[1]), 16) + " "
                + QString::number(static_cast<unsigned char>(data[2]), 16) + " "
                + QString::number(static_cast<unsigned char>(data[3]), 16));
            // PNG: 89 50 4E 47
            if (static_cast<unsigned char>(data[0]) == 0x89 &&
                static_cast<unsigned char>(data[1]) == 0x50 &&
                static_cast<unsigned char>(data[2]) == 0x4E &&
                static_cast<unsigned char>(data[3]) == 0x47) {
                isImage = true;
                Logger::instance().info("parseImageHls: detected PNG");
            }
            // JPEG: FF D8 FF
            if (static_cast<unsigned char>(data[0]) == 0xFF &&
                static_cast<unsigned char>(data[1]) == 0xD8 &&
                static_cast<unsigned char>(data[2]) == 0xFF) {
                isImage = true;
                Logger::instance().info("parseImageHls: detected JPEG");
            }
            // WebP: RIFF....WEBP
            if (data.size() >= 12 &&
                data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
                data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
                isImage = true;
                Logger::instance().info("parseImageHls: detected WebP");
            }
        }
    } else {
        Logger::instance().error("parseImageHls: download failed: " + reply->errorString());
    }
    reply->deleteLater();

    if (!isImage) {
        Logger::instance().info("parseImageHls: not an image, falling back to FFmpeg");
        return false;
    }

    imageHlsSegments_ = std::move(segments);
    imageHlsDuration_ = currentPts;
    return true;
}

bool MediaSession::isImageHls() const {
    return imageHls_;
}

const std::vector<ImageHlsSegment> &MediaSession::imageHlsSegments() const {
    return imageHlsSegments_;
}

double MediaSession::imageHlsDuration() const {
    return imageHlsDuration_;
}

bool MediaSession::isOpen() const {
    return formatContext_ != nullptr || imageHls_;
}

bool MediaSession::isNetwork() const {
    return network_;
}

bool MediaSession::isSeekable() const {
    if (imageHls_) return true;
    if (!formatContext_) return false;
    if (network_) {
        return formatContext_->duration > 0;
    }
    return true;
}

bool MediaSession::isLive() const {
    if (imageHls_) return false;
    if (!formatContext_ || !network_) return false;
    return formatContext_->duration <= 0;
}

double MediaSession::bufferProgress() const {
    if (imageHls_) return 1.0;
    if (!formatContext_) return -1;
    if (!network_) return 1.0;

    AVIOContext *pb = formatContext_->pb;
    if (!pb) return -1;

    int64_t pos = avio_tell(pb);
    int64_t size = avio_size(pb);
    if (size <= 0) return -1;

    return qBound(0.0, static_cast<double>(pos) / static_cast<double>(size), 1.0);
}

AVFormatContext *MediaSession::formatContext() const {
    return formatContext_.get();
}
