#include "models/LegadoSourceModel.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include "infrastructure/Logger.h"

namespace {

constexpr int kImportTimeoutMs = 30000;

}  // namespace

LegadoSourceModel::LegadoSourceModel(QObject *parent)
    : QAbstractListModel(parent) {
    loadPersisted();
}

int LegadoSourceModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : sources_.size();
}

QVariant LegadoSourceModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= sources_.size())
        return {};
    const BookSource &source = sources_.at(index.row());
    switch (role) {
    case NameRole: return source.name;
    case UrlRole: return source.url;
    case GroupRole: return source.group;
    case TypeRole: return source.type;
    case EnabledRole: return source.enabled;
    case SelectedRole: return index.row() == selectedIndex_;
    case CommentRole: return source.comment;
    default: return {};
    }
}

QHash<int, QByteArray> LegadoSourceModel::roleNames() const {
    return {
        {NameRole, "name"},
        {UrlRole, "url"},
        {GroupRole, "group"},
        {TypeRole, "type"},
        {EnabledRole, "enabled"},
        {SelectedRole, "selected"},
        {CommentRole, "comment"}
    };
}

int LegadoSourceModel::count() const { return sources_.size(); }
bool LegadoSourceModel::importing() const { return importing_; }
QString LegadoSourceModel::message() const { return message_; }
int LegadoSourceModel::selectedIndex() const { return selectedIndex_; }
bool LegadoSourceModel::hasSelected() const {
    return selectedIndex_ >= 0 && selectedIndex_ < sources_.size();
}

BookSource LegadoSourceModel::sourceAt(int row) const {
    return (row >= 0 && row < sources_.size()) ? sources_.at(row) : BookSource{};
}

BookSource LegadoSourceModel::selectedSource() const {
    return sourceAt(selectedIndex_);
}

QString LegadoSourceModel::sourceVariable(int row) const {
    return sourceAt(row).variable;
}

void LegadoSourceModel::setSourceVariable(int row, const QString &variable) {
    if (row < 0 || row >= sources_.size())
        return;
    sources_[row].variable = variable;
    if (row == selectedIndex_)
        emit activeSourceChanged();
}

void LegadoSourceModel::setLoading(bool loading) {
    if (importing_ == loading)
        return;
    importing_ = loading;
    emit importingChanged();
}

void LegadoSourceModel::setMessage(const QString &message) {
    if (message_ == message)
        return;
    message_ = message;
    emit messageChanged();
}

void LegadoSourceModel::setSelectedIndex(int row) {
    if (selectedIndex_ == row)
        return;
    if (selectedIndex_ >= 0 && selectedIndex_ < sources_.size()) {
        const QModelIndex previous = index(selectedIndex_, 0);
        emit dataChanged(previous, previous, {SelectedRole});
    }
    selectedIndex_ = row;
    if (selectedIndex_ >= 0 && selectedIndex_ < sources_.size()) {
        const QModelIndex current = index(selectedIndex_, 0);
        emit dataChanged(current, current, {SelectedRole});
    }
    emit selectedIndexChanged();
    emit activeSourceChanged();
}

void LegadoSourceModel::selectSource(int row) {
    if (row < 0 || row >= sources_.size())
        return;
    setSelectedIndex(row);
    persist();
}

void LegadoSourceModel::setEnabled(int row, bool enabled) {
    if (row < 0 || row >= sources_.size())
        return;
    sources_[row].enabled = enabled;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, {EnabledRole});
    if (row == selectedIndex_)
        emit activeSourceChanged();
    persist();
}

void LegadoSourceModel::removeSource(int row) {
    if (row < 0 || row >= sources_.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    sources_.removeAt(row);
    endRemoveRows();
    if (selectedIndex_ >= sources_.size())
        setSelectedIndex(sources_.isEmpty() ? -1 : sources_.size() - 1);
    else if (row <= selectedIndex_ && selectedIndex_ > 0)
        setSelectedIndex(selectedIndex_ - 1);
    emit countChanged();
    persist();
}

void LegadoSourceModel::clearAll() {
    beginResetModel();
    sources_.clear();
    selectedIndex_ = -1;
    endResetModel();
    emit countChanged();
    emit selectedIndexChanged();
    emit activeSourceChanged();
    persist();
    setMessage(QStringLiteral("已清空全部书源"));
}

int LegadoSourceModel::upsertSource(const BookSource &source) {
    if (source.url.isEmpty())
        return -1;
    for (int i = 0; i < sources_.size(); ++i) {
        if (sources_.at(i).url == source.url && sources_.at(i).name == source.name) {
            sources_[i] = source;
            const QModelIndex modelIndex = index(i, 0);
            emit dataChanged(modelIndex, modelIndex);
            return i;
        }
    }
    beginInsertRows(QModelIndex(), sources_.size(), sources_.size());
    sources_.append(source);
    endInsertRows();
    return sources_.size() - 1;
}

void LegadoSourceModel::importFromUrl(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        setMessage(QStringLiteral("请输入书源链接"));
        return;
    }
    setLoading(true);
    setMessage(QStringLiteral("正在下载书源…"));

    QNetworkRequest request{QUrl(trimmed)};
    request.setTransferTimeout(kImportTimeoutMs);
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    QNetworkReply *reply = nam_.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        setLoading(false);
        if (reply->error() != QNetworkReply::NoError) {
            setMessage(QStringLiteral("下载失败: %1").arg(reply->errorString()));
            return;
        }
        mergeJson(reply->readAll(), reply->url().toString());
    });
}

void LegadoSourceModel::importFromText(const QString &text) {
    mergeJson(text.toUtf8(), QStringLiteral("粘贴内容"));
}

void LegadoSourceModel::mergeJson(const QByteArray &bytes, const QString &origin) {
    QJsonParseError parseError{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (!doc.isNull()) {
        if (doc.isArray()) {
            int added = 0;
            const QJsonArray array = doc.array();
            for (const QJsonValue &value : array) {
                if (value.isObject()) {
                    upsertSource(BookSource::fromJson(value.toObject()));
                    ++added;
                }
            }
            persist();
            emit countChanged();
            if (selectedIndex_ < 0)
                setSelectedIndex(0);
            setMessage(QStringLiteral("从 %1 导入 %2 个书源").arg(origin).arg(added));
            Logger::instance().info(QStringLiteral("[书源] 导入 %1 个书源来自 %2")
                .arg(added).arg(origin));
            return;
        }
        if (doc.isObject()) {
            upsertSource(BookSource::fromJson(doc.object()));
            persist();
            emit countChanged();
            if (selectedIndex_ < 0)
                setSelectedIndex(0);
            setMessage(QStringLiteral("导入书源: %1")
                .arg(BookSource::fromJson(doc.object()).name));
            return;
        }
    }
    setMessage(QStringLiteral("解析失败: 不是有效的书源 JSON (%1)")
        .arg(parseError.errorString()));
    Logger::instance().warn(QStringLiteral("[书源] 书源 JSON 解析失败: %1")
        .arg(parseError.errorString()));
}

void LegadoSourceModel::loadPersisted() {
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject object = doc.object();
    const QJsonArray array = object.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &value : array) {
        if (value.isObject())
            sources_.append(BookSource::fromJson(value.toObject()));
    }
    selectedIndex_ = object.value(QStringLiteral("selected")).toInt(-1);
    if (selectedIndex_ >= sources_.size())
        selectedIndex_ = sources_.isEmpty() ? -1 : 0;
    emit countChanged();
}

void LegadoSourceModel::persist() const {
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray array;
    for (const BookSource &source : sources_)
        array.append(source.toJson());
    QJsonObject object;
    object.insert(QStringLiteral("sources"), array);
    object.insert(QStringLiteral("selected"), selectedIndex_);
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.commit();
}

QString LegadoSourceModel::storagePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/legado_sources.json");
}
