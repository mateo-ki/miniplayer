#pragma once

#include <QQuickPaintedItem>
#include <QImage>

class VideoFrameBridge : public QQuickPaintedItem {
    Q_OBJECT
public:
    enum AspectRatio { Keep, Fill, Force4_3, Force16_9 };
    Q_ENUM(AspectRatio)

    Q_PROPERTY(AspectRatio aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY aspectRatioChanged)

    explicit VideoFrameBridge(QQuickItem *parent = nullptr);

    void present(const QImage &frame);
    Q_INVOKABLE QImage currentFrame() const;

    AspectRatio aspectRatio() const { return aspectRatio_; }
    void setAspectRatio(AspectRatio ratio);
    void setSubtitleText(const QString &text);

signals:
    void aspectRatioChanged();

protected:
    void paint(QPainter *painter) override;

private:
    QImage currentFrame_;
    AspectRatio aspectRatio_ = Keep;
    QString subtitleText_;
};
