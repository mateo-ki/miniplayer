#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class BeeClient;

/**
 * 蜜蜂影视 追剧日历 (jk = /film-schedule-model/select) 模型。
 *
 * 该接口返回的是「按天分组」的更新日历 (date/weekday/items),而不是分页列表,
 * 因此不继承 QAbstractListModel,而是把整段日历作为一个 QVariantList 暴露给
 * QML,由视图自行按天渲染。每项 items 内含 {vodId,title,cover,episodeStatus,
 * deltaEpisode}。
 */
class BeeScheduleModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantList days READ days NOTIFY daysChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
public:
    explicit BeeScheduleModel(QObject *parent = nullptr);

    bool loading() const;
    QVariantList days() const;
    QString errorMessage() const;

    Q_INVOKABLE void load(const QString &tag = {});
    Q_INVOKABLE void clear();

signals:
    void loadingChanged();
    void daysChanged();
    void errorMessageChanged();

private:
    BeeClient *client();
    void setLoading(bool value);
    void setError(const QString &value);

    bool loading_ = false;
    QVariantList days_;
    QString error_;
    quint64 requestSerial_ = 0;
};
