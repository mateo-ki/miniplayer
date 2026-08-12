#include "render/VideoFrameBridge.h"

#include <QPainter>

#include "infrastructure/Logger.h"

VideoFrameBridge::VideoFrameBridge(QQuickItem *parent)
    : QQuickPaintedItem(parent) {
    setFillColor(Qt::black);
}

void VideoFrameBridge::present(const QImage &frame) {
    currentFrame_ = frame;
    if (frame.isNull()) {
        Logger::instance().warn("VideoFrameBridge::present called with null frame");
    }
    update();
}

QImage VideoFrameBridge::currentFrame() const {
    return currentFrame_;
}

void VideoFrameBridge::setAspectRatio(AspectRatio ratio) {
    if (aspectRatio_ == ratio) return;
    aspectRatio_ = ratio;
    update();
    emit aspectRatioChanged();
}

void VideoFrameBridge::setSubtitleText(const QString &text) {
    subtitleText_ = text;
    update();
}

void VideoFrameBridge::paint(QPainter *painter) {
    if (currentFrame_.isNull()) {
        return;
    }

    QRectF target = boundingRect();
    QSizeF source = currentFrame_.size();

    QSizeF scaled;
    switch (aspectRatio_) {
    case Fill:
        scaled = source.scaled(target.size(), Qt::KeepAspectRatioByExpanding);
        break;
    case Force4_3:
        scaled = QSizeF(target.height() * 4.0 / 3.0, target.height());
        if (scaled.width() > target.width()) {
            scaled = QSizeF(target.width(), target.width() * 3.0 / 4.0);
        }
        break;
    case Force16_9:
        scaled = QSizeF(target.height() * 16.0 / 9.0, target.height());
        if (scaled.width() > target.width()) {
            scaled = QSizeF(target.width(), target.width() * 9.0 / 16.0);
        }
        break;
    case Keep:
    default:
        scaled = source.scaled(target.size(), Qt::KeepAspectRatio);
        break;
    }

    QRectF centered(target.x() + (target.width() - scaled.width()) / 2,
                    target.y() + (target.height() - scaled.height()) / 2,
                    scaled.width(), scaled.height());

    painter->drawImage(centered, currentFrame_);

    // Draw subtitle text
    if (!subtitleText_.isEmpty()) {
        QFont font("Arial", 18, QFont::Bold);
        painter->setFont(font);

        // Position at bottom center of the video area
        QRectF subtitleRect(centered.x(), centered.bottom() - 60, centered.width(), 50);

        // Draw text shadow for readability
        painter->setPen(QColor(0, 0, 0, 200));
        painter->drawText(subtitleRect.translated(1, 1), Qt::AlignHCenter | Qt::AlignBottom, subtitleText_);

        // Draw text
        painter->setPen(Qt::white);
        painter->drawText(subtitleRect, Qt::AlignHCenter | Qt::AlignBottom, subtitleText_);
    }
}
