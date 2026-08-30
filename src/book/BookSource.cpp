#include "book/BookSource.h"

namespace {

QVariantMap toStringMap(const QJsonValue &value) {
    QVariantMap result;
    if (!value.isObject())
        return result;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isString())
            result.insert(it.key(), it.value().toString());
        else if (!it.value().isNull() && !it.value().isUndefined())
            result.insert(it.key(), it.value().toVariant().toString());
    }
    return result;
}

}  // namespace

BookSource BookSource::fromJson(const QJsonObject &object) {
    BookSource source;
    source.name = object.value(QStringLiteral("bookSourceName")).toString();
    source.url = object.value(QStringLiteral("bookSourceUrl")).toString().trimmed();
    source.group = object.value(QStringLiteral("bookSourceGroup")).toString();
    source.comment = object.value(QStringLiteral("bookSourceComment")).toString();
    source.type = object.value(QStringLiteral("bookSourceType")).toInt();
    source.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    source.enabledExplore = object.value(QStringLiteral("enabledExplore")).toBool(true);
    source.searchUrl = object.value(QStringLiteral("searchUrl")).toString();
    source.exploreUrl = object.value(QStringLiteral("exploreUrl")).toString();
    source.header = object.value(QStringLiteral("header")).toString();
    source.loginUrl = object.value(QStringLiteral("loginUrl")).toString();
    source.jsLib = object.value(QStringLiteral("jsLib")).toString();
    source.variable = object.value(QStringLiteral("variable")).toString();
    source.ruleSearch = toStringMap(object.value(QStringLiteral("ruleSearch")));
    source.ruleExplore = toStringMap(object.value(QStringLiteral("ruleExplore")));
    source.ruleBookInfo = toStringMap(object.value(QStringLiteral("ruleBookInfo")));
    source.ruleToc = toStringMap(object.value(QStringLiteral("ruleToc")));
    source.ruleContent = toStringMap(object.value(QStringLiteral("ruleContent")));
    return source;
}

QJsonObject BookSource::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("bookSourceName"), name);
    object.insert(QStringLiteral("bookSourceUrl"), url);
    object.insert(QStringLiteral("bookSourceGroup"), group);
    object.insert(QStringLiteral("bookSourceComment"), comment);
    object.insert(QStringLiteral("bookSourceType"), type);
    object.insert(QStringLiteral("enabled"), enabled);
    object.insert(QStringLiteral("enabledExplore"), enabledExplore);
    object.insert(QStringLiteral("searchUrl"), searchUrl);
    object.insert(QStringLiteral("exploreUrl"), exploreUrl);
    object.insert(QStringLiteral("header"), header);
    object.insert(QStringLiteral("loginUrl"), loginUrl);
    object.insert(QStringLiteral("jsLib"), jsLib);
    object.insert(QStringLiteral("variable"), variable);
    object.insert(QStringLiteral("ruleSearch"), QJsonObject::fromVariantMap(ruleSearch));
    object.insert(QStringLiteral("ruleExplore"), QJsonObject::fromVariantMap(ruleExplore));
    object.insert(QStringLiteral("ruleBookInfo"), QJsonObject::fromVariantMap(ruleBookInfo));
    object.insert(QStringLiteral("ruleToc"), QJsonObject::fromVariantMap(ruleToc));
    object.insert(QStringLiteral("ruleContent"), QJsonObject::fromVariantMap(ruleContent));
    return object;
}

const QVariantMap *BookSource::rules(int kind) const {
    switch (kind) {
    case 0: return &ruleSearch;
    case 1: return &ruleExplore;
    case 2: return &ruleBookInfo;
    case 3: return &ruleToc;
    case 4: return &ruleContent;
    default: return nullptr;
    }
}
