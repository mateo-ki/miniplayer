#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>
#include <QString>

// Legado (阅读 3.x) 书源数据结构。规则字段保持原始字符串，
// 由 RuleEngine 按需解析，避免导入时做有损转换。
class BookSource final {
public:
    QString name;
    QString url;          // bookSourceUrl，作为相对链接的基准
    QString group;
    QString comment;
    int type = 0;         // 0 文本 1 音频 2 图片 3 文件夹
    bool enabled = true;
    bool enabledExplore = true;
    QString searchUrl;
    QString exploreUrl;
    QString header;       // JSON 对象字符串
    QString loginUrl;
    QString jsLib;        // 源级 JS 库，评估规则前注入全局
    QString variable;     // 源变量（source.getVariable）

    QVariantMap ruleSearch;
    QVariantMap ruleExplore;
    QVariantMap ruleBookInfo;
    QVariantMap ruleToc;
    QVariantMap ruleContent;

    static BookSource fromJson(const QJsonObject &object);
    QJsonObject toJson() const;

    const QVariantMap *rules(int kind) const;
};
