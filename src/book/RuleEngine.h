#pragma once

#include <QHash>
#include <QJSEngine>
#include <QJSValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "book/BookSource.h"
#include "book/HtmlDom.h"

class LegadoClient;

// 规则评估时的当前对象：一个 HTML 元素、一个 JSON 节点或纯文本。
struct RuleNode {
    HtmlNode::Ptr element;
    QJsonValue json;
    QString text;
    bool hasJson = false;

    static RuleNode fromText(const QString &value);
    static RuleNode fromElement(HtmlNode::Ptr el);
    static RuleNode fromJson(QJsonValue value);
    QString asText() const;
};

// 一次规则评估的外部输入（对应 Legado AnalyzeRule 的环境变量）。
struct RuleContext {
    QString result;      // 当前抓取到的响应文本
    QString baseUrl;     // 响应的最终地址
    QString key;         // 搜索关键字
    int page = 1;
    QString title;
    const BookSource *source = nullptr;
    std::function<QString()> getVariable;
    std::function<void(const QString &)> setVariable;
};

// Legado 书源规则引擎。支持：
//  - @js: 与 <js>...</js>（QJSEngine，绑定 result/baseUrl/key/page/source/java）
//  - JSONPath（$.a.b[0]、$.data[*]、$..name，含 @Json: 前缀）
//  - JSoup 默认查询（class.x / id.x / tag.x / .x / #x / [n] / children /
//    text / textNodes / ownText / html / 属性名）
//  - @css: 简化 CSS 选择器（空格后代、> 直接子级）
//  - ## 正则替换、|| 回退、&& 合并、{{key}}/{{page}}/{{js 表达式}} 模板
//  - jsLib 预载与 source.getVariable/setVariable/put/get
// 不支持 XPath（返回空并记录一次日志）。
class RuleEngine final : public QObject {
    Q_OBJECT
public:
    explicit RuleEngine(LegadoClient *client, QObject *parent = nullptr);

    QStringList evaluateStrings(const QString &rule, const RuleContext &ctx,
                                const RuleNode &current);
    QString evaluateString(const QString &rule, const RuleContext &ctx,
                           const RuleNode &current);

    // 列表规则（bookList/chapterList）：返回元素/JSON 对象节点列表。
    QVector<RuleNode> evaluateList(const QString &rule, const RuleContext &ctx);

    // {{...}} 模板替换（URL 与正文字符串共用）。
    // 内层支持 key/page 字面量、$.xxx JSONPath（按 current 求值）与 JS 表达式。
    QString evalTemplate(const QString &input, const RuleContext &ctx,
                         const RuleNode &current = RuleNode());

    // JS 环境管理
    void loadJsLib(const BookSource *source);
    void setActiveSource(const BookSource *source);

    // ---- 由内置 JS shim 调用（source/java 对象） ----
    Q_INVOKABLE QString cppGetVariable();
    Q_INVOKABLE void cppSetVariable(const QString &value);
    Q_INVOKABLE void cppPut(const QString &key, const QString &value);
    Q_INVOKABLE QString cppGet(const QString &key);
    Q_INVOKABLE QString cppHttpGet(const QString &url, const QJSValue &headers);
    Q_INVOKABLE QString cppHttpPost(const QString &url, const QString &body,
                                    const QJSValue &headers);
    Q_INVOKABLE QString cppAjax(const QJSValue &arg);
    Q_INVOKABLE void cppLog(const QString &message);

private:
    QVector<RuleNode> evalRule(const QString &rule, const RuleContext &ctx,
                               const RuleNode &current);
    QVector<RuleNode> evalJs(const QString &code, const RuleContext &ctx,
                             const RuleNode &current, const QString &tail);
    QVector<RuleNode> evalJsonPath(const QString &path, const RuleNode &current,
                                   const RuleContext &ctx);
    QVector<RuleNode> evalDefault(const QString &rule, const RuleContext &ctx,
                                  const RuleNode &current);
    QVector<RuleNode> evalCss(const QString &rule, const RuleContext &ctx,
                              const RuleNode &current);
    QVector<RuleNode> evalElements(const QList<HtmlNode::Ptr> &elements,
                                   const QString &extraction);

    QJSValue evalJsValue(const QString &code, const RuleContext &ctx,
                         const RuleNode &current);
    void prepareJsGlobals(const RuleContext &ctx, const RuleNode &current);
    QString runJsString(const QString &code, const RuleContext &ctx,
                        const RuleNode &current);
    QJsonValue jsonRoot(const RuleNode &current, const RuleContext &ctx, bool *ok) const;
    QString sourceKey() const;
    QMap<QString, QString> jsValueToHeaderMap(const QJSValue &value) const;

    QJSEngine engine_;
    LegadoClient *client_ = nullptr;
    const BookSource *activeSource_ = nullptr;
    QString loadedJsLib_;
    QHash<QString, QString> sourceMemory_;
    QSet<QString> warned_;
    std::function<QString()> variableProvider_;
    std::function<void(const QString &)> variableSetter_;
    RuleContext currentKeyContext_;
};
