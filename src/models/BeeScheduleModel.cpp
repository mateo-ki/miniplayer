#include "BeeScheduleModel.h"
#include "media/BeeClient.h"

#include <QCoreApplication>

BeeScheduleModel::BeeScheduleModel(QObject *parent)
    : QObject(parent) {}

bool BeeScheduleModel::loading() const { return loading_; }
QVariantList BeeScheduleModel::days() const { return days_; }
QString BeeScheduleModel::errorMessage() const { return error_; }

void BeeScheduleModel::setLoading(bool value) {
    if (loading_ == value) return;
    loading_ = value;
    emit loadingChanged();
}

void BeeScheduleModel::setError(const QString &value) {
    if (error_ == value) return;
    error_ = value;
    emit errorMessageChanged();
}

BeeClient *BeeScheduleModel::client() {
    static BeeClient *instance = new BeeClient(qApp);
    return instance;
}

void BeeScheduleModel::load(const QString &tag) {
    const QString trimmedTag = tag.trimmed();
    const quint64 serial = ++requestSerial_;
    setLoading(true);
    setError({});
    client()->filmSchedule(trimmedTag,
                          [this, serial](bool ok, const QVariantList &days,
                                          const QString &error) {
        if (serial != requestSerial_)
            return;
        setLoading(false);
        if (!ok) {
            setError(error);
            return;
        }
        days_ = days;
        emit daysChanged();
    });
}

void BeeScheduleModel::clear() {
    ++requestSerial_;
    days_.clear();
    emit daysChanged();
    setLoading(false);
    setError({});
}
