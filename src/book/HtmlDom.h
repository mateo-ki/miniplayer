#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>

// 面向书源规则的容错 HTML DOM。书源默认规则只需要 tag/class/id/属性/文本
// 这几类查询，不追求与浏览器一致的解析，只保证常见网页结构可用。
class HtmlNode : public std::enable_shared_from_this<HtmlNode> {
public:
    using Ptr = std::shared_ptr<HtmlNode>;

    QString tag;                     // 小写标签名；文本节点没有对应标签
    QMap<QString, QString> attrs;
    QList<Ptr> children;
    HtmlNode *parent = nullptr;
    QStringList textRuns;            // 直接子文本片段

    bool isElement() const { return !tag.isEmpty(); }
    QString attr(const QString &name) const;
    QString ownText() const;         // 直接子文本合并
    QString allText() const;         // 递归文本
    QString outerHtml();
    QString innerHtml() const;

    // 递归查找后代（需要 shared_from_this，无法为 const 方法）
    QList<Ptr> descendantsByTag(const QString &tag);      // tag == "*" 匹配全部
    QList<Ptr> descendantsByClass(const QString &className);
    QList<Ptr> descendantsById(const QString &id);
    QList<Ptr> directChildren() const { return children; }

    bool matchesSimple(const QString &simple) const;  // tag/.class/#id/[a=v] 组合
};

class HtmlDom final {
public:
    static HtmlNode::Ptr parse(const QString &html);
    static QString decodeEntities(QString text);
};
