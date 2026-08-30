#include "book/RuleEngine.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSValueIterator>
#include <QRegularExpression>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <functional>

#include "book/LegadoClient.h"
#include "infrastructure/Logger.h"

namespace {

// 引擎启动脚本：定义 source/java/cookie 三个全局对象，方法转发给 C++。
// 注意全局名必须与 Legado 规则约定一致（source/java/cookie），不能改名。
const char *kJsBootstrap = R"JS(
var source = {
    bookSourceUrl: '',
    bookSourceName: '',
    getVariable: function () { return __native.cppGetVariable(); },
    setVariable: function (v) { __native.cppSetVariable(String(v)); },
    put: function (k, v) { __native.cppPut(String(k), String(v)); },
    get: function (k) { return __native.cppGet(String(k)); },
    getLoginInfoMap: function () { return {}; }
};
var java = {
    get: function (url, headers) { return __native.cppHttpGet(String(url), headers); },
    post: function (url, body, headers) { return __native.cppHttpPost(String(url), String(body), headers); },
    ajax: function (arg) { return __native.cppAjax(arg); },
    log: function () {
        var parts = Array.prototype.slice.call(arguments);
        __native.cppLog(parts.map(String).join(' '));
    },
    toast: function (msg) { __native.cppLog('[toast] ' + String(msg)); },
    startBrowser: function (url) { __native.cppLog('[browser] ' + String(url)); },
    getCookie: function () { return ''; },
    setCookie: function () {},
    timeFormat: function (ms) {
        var d = new Date(parseInt(ms));
        function p(n) { return n < 10 ? '0' + n : '' + n; }
        return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate());
    }
};
var cookie = {
    getCookie: function () { return ''; },
    setCookie: function () {}
};
)JS";

const QSet<QString> kExtractionKeywords = {
    QStringLiteral("text"), QStringLiteral("textNodes"), QStringLiteral("ownText"),
    QStringLiteral("html"), QStringLiteral("all")
};

const QSet<QString> kCommonTags = {
    QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("body"), QStringLiteral("br"),
    QStringLiteral("button"), QStringLiteral("dd"), QStringLiteral("div"), QStringLiteral("dl"),
    QStringLiteral("dt"), QStringLiteral("em"), QStringLiteral("font"), QStringLiteral("form"),
    QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"),
    QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("head"), QStringLiteral("header"),
    QStringLiteral("hr"), QStringLiteral("html"), QStringLiteral("i"), QStringLiteral("img"),
    QStringLiteral("input"), QStringLiteral("li"), QStringLiteral("label"), QStringLiteral("main"),
    QStringLiteral("meta"), QStringLiteral("nav"), QStringLiteral("ol"), QStringLiteral("option"),
    QStringLiteral("p"), QStringLiteral("pre"), QStringLiteral("s"), QStringLiteral("script"),
    QStringLiteral("section"), QStringLiteral("select"), QStringLiteral("small"),
    QStringLiteral("span"), QStringLiteral("strong"), QStringLiteral("style"),
    QStringLiteral("table"), QStringLiteral("tbody"), QStringLiteral("td"),
    QStringLiteral("textarea"), QStringLiteral("tfoot"), QStringLiteral("th"),
    QStringLiteral("thead"), QStringLiteral("title"), QStringLiteral("tr"), QStringLiteral("u"),
    QStringLiteral("ul"), QStringLiteral("video"), QStringLiteral("source")
};

QString jsonValueToText(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (d == std::floor(d) && !qIsInf(d) && !qIsNaN(d))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isNull() || value.isUndefined())
        return QString();
    const QJsonDocument doc(value.isArray() ? QJsonDocument(value.toArray())
                                            : QJsonDocument(value.toObject()));
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

// 判断 pos 是否处于 <js>...</js> 或 {{...}} 结构内部。
bool isTopLevelPos(const QString &rule, qsizetype pos) {
    qsizetype i = 0;
    while (i < pos) {
        const int jsStart = static_cast<int>(rule.indexOf(QStringLiteral("<js>"), i));
        const qsizetype open = rule.indexOf(QStringLiteral("{{"), i);
        if (jsStart >= 0 && (open < 0 || jsStart < open)) {
            const int jsEnd = static_cast<int>(rule.indexOf(QStringLiteral("</js>"), jsStart + 4));
            const qsizetype blockEnd = jsEnd >= 0 ? jsEnd + 5 : rule.size();
            if (pos >= jsStart && pos < blockEnd)
                return false;
            i = blockEnd;
            continue;
        }
        if (open >= 0) {
            const int close = static_cast<int>(rule.indexOf(QStringLiteral("}}"), open + 2));
            const qsizetype blockEnd = close >= 0 ? close + 2 : rule.size();
            if (pos >= open && pos < blockEnd)
                return false;
            i = blockEnd;
            continue;
        }
        break;
    }
    return true;
}

qsizetype findTopLevel(const QString &rule, const QString &needle, qsizetype from = 0) {
    qsizetype pos = qMax<qsizetype>(from, 0);
    while (true) {
        pos = rule.indexOf(needle, pos);
        if (pos < 0)
            return -1;
        if (isTopLevelPos(rule, pos))
            return pos;
        pos += needle.size();
    }
}

QList<QString> splitTopLevel(const QString &rule, const QString &separator) {
    QList<QString> parts;
    qsizetype start = 0;
    while (true) {
        const qsizetype pos = findTopLevel(rule, separator, start);
        if (pos < 0)
            break;
        parts.append(rule.mid(start, pos - start));
        start = pos + separator.size();
    }
    parts.append(rule.mid(start));
    return parts;
}

// Java 风格正则替换：$N 为捕获组引用（Qt 自带替换串的 \N 语义有歧义，
// 手工展开以保证书源正则与 Legado 行为一致）。
QString javaStyleReplace(const QString &text, const QRegularExpression &re,
                         const QString &replacement) {
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out.append(text.mid(last, match.capturedStart() - last));
        static const QRegularExpression groupRef(QStringLiteral(R"(\$(\d+))"));
        qsizetype pos = 0;
        while (true) {
            const qsizetype dollar = replacement.indexOf(QLatin1Char('$'), pos);
            if (dollar < 0)
                break;
            qsizetype digits = dollar + 1;
            while (digits < replacement.size() && replacement.at(digits).isDigit())
                ++digits;
            if (digits == dollar + 1) {
                out.append(replacement.mid(pos, dollar - pos + 1));
                pos = dollar + 1;
                continue;
            }
            out.append(replacement.mid(pos, dollar - pos));
            bool ok = false;
            const int group = replacement.mid(dollar + 1, digits - dollar - 1).toInt(&ok);
            const QString captured = ok ? match.captured(group) : QString();
            out.append(captured);
            pos = digits;
        }
        out.append(replacement.mid(pos));
        last = match.capturedEnd();
    }
    out.append(text.mid(last));
    return out;
}

}  // namespace

RuleNode RuleNode::fromText(const QString &value) {
    RuleNode node;
    node.text = value;
    return node;
}

RuleNode RuleNode::fromElement(HtmlNode::Ptr el) {
    RuleNode node;
    node.element = std::move(el);
    return node;
}

RuleNode RuleNode::fromJson(QJsonValue value) {
    RuleNode node;
    node.json = std::move(value);
    node.hasJson = true;
    return node;
}

QString RuleNode::asText() const {
    if (element)
        return element->allText();
    if (hasJson)
        return jsonValueToText(json);
    return text;
}

RuleEngine::RuleEngine(LegadoClient *client, QObject *parent)
    : QObject(parent), client_(client) {
    // __native 包装的是 this 本身，必须声明为 C++ 所有权，
    // 否则 QJSEngine 析构时会 delete 正在析构的 RuleEngine 导致崩溃。
    engine_.globalObject().setProperty(QStringLiteral("__native"), engine_.newQObject(this));
    QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
    const QJSValue result = engine_.evaluate(QString::fromUtf8(kJsBootstrap));
    if (result.isError())
        Logger::instance().warn(QStringLiteral("[书源] JS bootstrap 失败: %1")
            .arg(result.toString()));
}

QString RuleEngine::sourceKey() const {
    return activeSource_ ? activeSource_->url : QStringLiteral("*global*");
}

void RuleEngine::setActiveSource(const BookSource *source) {
    activeSource_ = source;
}

void RuleEngine::loadJsLib(const BookSource *source) {
    if (!source || source->jsLib.trimmed().isEmpty())
        return;
    const QString signature = source->url + QLatin1Char('|') + source->jsLib;
    if (loadedJsLib_ == signature)
        return;
    activeSource_ = source;
    const QJSValue result = engine_.evaluate(source->jsLib);
    if (result.isError())
        Logger::instance().warn(QStringLiteral("[书源] jsLib 执行失败 (%1): %2")
            .arg(source->name, result.toString().left(200)));
    else
        loadedJsLib_ = signature;
}

// ---- JS 绑定实现 ----

QString RuleEngine::cppGetVariable() {
    return variableProvider_ ? variableProvider_() : QString();
}

void RuleEngine::cppSetVariable(const QString &value) {
    if (variableSetter_)
        variableSetter_(value);
}

void RuleEngine::cppPut(const QString &key, const QString &value) {
    sourceMemory_.insert(sourceKey() + QLatin1Char('|') + key, value);
}

QString RuleEngine::cppGet(const QString &key) {
    return sourceMemory_.value(sourceKey() + QLatin1Char('|') + key);
}

QString RuleEngine::cppHttpGet(const QString &url, const QJSValue &headers) {
    return client_ ? client_->jsGet(url, jsValueToHeaderMap(headers)) : QString();
}

QString RuleEngine::cppHttpPost(const QString &url, const QString &body,
                                const QJSValue &headers) {
    return client_ ? client_->jsPost(url, body, jsValueToHeaderMap(headers)) : QString();
}

QString RuleEngine::cppAjax(const QJSValue &arg) {
    if (!client_)
        return QString();
    LegadoFetchOptions options;
    QString url;
    if (arg.isObject() && !arg.isArray()) {
        url = arg.property(QStringLiteral("url")).toString();
        const QString method = arg.property(QStringLiteral("method")).toString();
        if (!method.isEmpty())
            options.method = method.toUpper();
        options.body = arg.property(QStringLiteral("body")).toString();
        options.charset = arg.property(QStringLiteral("charset")).toString();
        options.headers = jsValueToHeaderMap(arg.property(QStringLiteral("headers")));
    } else {
        url = arg.toString();
    }
    if (url.isEmpty())
        return QString();
    const LegadoFetchResult result = client_->fetch(QUrl(url), options, activeSource_);
    return result.ok ? result.text : QString();
}

void RuleEngine::cppLog(const QString &message) {
    Logger::instance().info(QStringLiteral("[书源JS] %1").arg(message.left(500)));
}

QMap<QString, QString> RuleEngine::jsValueToHeaderMap(const QJSValue &value) const {
    QMap<QString, QString> headers;
    if (value.isNull() || value.isUndefined() || !value.isObject())
        return headers;
    QJSValueIterator it(value);
    while (it.hasNext()) {
        it.next();
        if (it.name() == QLatin1String("constructor"))
            continue;
        headers.insert(it.name(), it.value().toString());
    }
    return headers;
}

// ---- 对外入口 ----

QString RuleEngine::evaluateString(const QString &rule, const RuleContext &ctx,
                                   const RuleNode &current) {
    const QStringList results = evaluateStrings(rule, ctx, current);
    return results.isEmpty() ? QString() : results.first();
}

QStringList RuleEngine::evaluateStrings(const QString &rule, const RuleContext &ctx,
                                        const RuleNode &current) {
    QStringList out;
    for (const RuleNode &node : evalRule(rule, ctx, current))
        out.append(node.asText());
    return out;
}

QVector<RuleNode> RuleEngine::evaluateList(const QString &rule, const RuleContext &ctx) {
    return evalRule(rule, ctx, RuleNode::fromText(ctx.result));
}

QString RuleEngine::evalTemplate(const QString &input, const RuleContext &ctx,
                                 const RuleNode &current) {
    if (!input.contains(QStringLiteral("{{")))
        return input;
    QString out;
    qsizetype start = 0;
    static const QRegularExpression pattern(QStringLiteral(R"(\{\{(.*?)\}\})"));
    auto it = pattern.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out.append(input.mid(start, m.capturedStart() - start));
        const QString expr = m.captured(1).trimmed();
        QString value;
        if (expr == QLatin1String("key") || expr == QLatin1String("searchKey"))
            value = ctx.key;
        else if (expr == QLatin1String("page") || expr == QLatin1String("searchPage"))
            value = QString::number(ctx.page);
        else if (expr.startsWith(QLatin1String("$.")) || expr.startsWith(QLatin1Char('$'))
                 || expr.startsWith(QLatin1String("@json:")) || expr.startsWith(QLatin1String("@Json:")))
            value = evaluateString(expr, ctx, current);
        else
            value = runJsString(expr, ctx, current);
        out.append(value);
        start = m.capturedEnd();
    }
    out.append(input.mid(start));
    return out;
}

// ---- 规则主流程 ----

QVector<RuleNode> RuleEngine::evalRule(const QString &rule, const RuleContext &ctx,
                                       const RuleNode &current) {
    QString trimmed = rule.trimmed();
    if (trimmed.isEmpty())
        return {};  // 空规则 → 空结果（Legado 语义），不能回退为当前文本

    currentKeyContext_ = ctx;

    // 1) 整条规则是 JS
    if (trimmed.startsWith(QLatin1String("@js:")))
        return evalJs(trimmed.mid(4), ctx, current, QString());

    // 2) <js>...</js> 块，可带前缀规则与后缀规则
    const qsizetype jsBlockStart = findTopLevel(trimmed, QStringLiteral("<js>"));
    if (jsBlockStart >= 0) {
        const int jsBlockEnd = static_cast<int>(
            trimmed.indexOf(QStringLiteral("</js>"), jsBlockStart));
        if (jsBlockEnd >= 0) {
            const QString prefix = trimmed.left(jsBlockStart).trimmed();
            const QString code = trimmed.mid(jsBlockStart + 4, jsBlockEnd - jsBlockStart - 4);
            QString tail = trimmed.mid(jsBlockEnd + 5).trimmed();
            if (tail.startsWith(QLatin1Char('@')) || tail.startsWith(QLatin1Char(':')))
                tail = tail.mid(1).trimmed();
            RuleNode jsInput = current;
            if (!prefix.isEmpty()) {
                const QVector<RuleNode> prefixNodes = evalRule(prefix, ctx, current);
                if (!prefixNodes.isEmpty())
                    jsInput = prefixNodes.first();
            }
            return evalJs(code, ctx, jsInput, tail);
        }
    }

    // 3) 规则尾部拼接 JS（如 $.book_id@js:'https://...' + result）
    const qsizetype jsTailPos = findTopLevel(trimmed, QStringLiteral("@js:"));
    if (jsTailPos > 0) {
        const QString left = trimmed.left(jsTailPos).trimmed();
        const QString code = trimmed.mid(jsTailPos + 4);
        QVector<RuleNode> nodes = evalRule(left, ctx, current);
        if (nodes.isEmpty())
            nodes.append(RuleNode::fromText(QString()));
        QVector<RuleNode> out;
        for (const RuleNode &node : nodes)
            out.append(evalJs(code, ctx, node, QString()));
        return out;
    }

    // 4) || 回退：取第一个非空分支
    const QList<QString> branches = splitTopLevel(trimmed, QStringLiteral("||"));
    if (branches.size() > 1) {
        for (const QString &branch : branches) {
            QVector<RuleNode> nodes = evalRule(branch.trimmed(), ctx, current);
            const bool hasContent = std::any_of(nodes.cbegin(), nodes.cend(),
                [](const RuleNode &n) { return !n.asText().isEmpty(); });
            if (hasContent)
                return nodes;
        }
        return {};
    }

    // 5) && 合并
    const QList<QString> parts = splitTopLevel(trimmed, QStringLiteral("&&"));
    if (parts.size() > 1) {
        QVector<RuleNode> merged;
        for (const QString &part : parts) {
            if (part.trimmed().isEmpty())
                continue;
            merged.append(evalRule(part.trimmed(), ctx, current));
        }
        return merged;
    }

    // 6) ## 正则替换
    const qsizetype replacePos = findTopLevel(trimmed, QStringLiteral("##"));
    QString replaceSpec;
    if (replacePos >= 0) {
        replaceSpec = trimmed.mid(replacePos + 2);
        trimmed = trimmed.left(replacePos).trimmed();
    }

    QVector<RuleNode> nodes;
    bool templated = false;
    if (trimmed.contains(QStringLiteral("{{"))) {
        // {{...}} 模板（内层可为 key/page、$.json 字段或 JS），展开后即结果
        trimmed = evalTemplate(trimmed, ctx, current);
        templated = true;
    }
    static const QRegularExpression bareFieldRe(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_-]*$"));
    if (templated || trimmed.contains(QStringLiteral("://"))) {
        // 常量文本或 URL（可能由模板拼接而成）
        nodes.append(RuleNode::fromText(trimmed));
    } else if (current.hasJson && bareFieldRe.match(trimmed).hasMatch()) {
        // JSON 源常用裸字段名（chapterName: "title"）
        nodes = evalJsonPath(QStringLiteral("$.") + trimmed, current, ctx);
    } else if (trimmed.startsWith(QLatin1String("@Json:")) || trimmed.startsWith(QLatin1String("@json:")))
        nodes = evalJsonPath(trimmed.mid(6).trimmed(), current, ctx);
    else if (trimmed.startsWith(QLatin1String("$.")) || trimmed == QLatin1String("$"))
        nodes = evalJsonPath(trimmed.mid(1), current, ctx);
    else if (trimmed.startsWith(QLatin1Char('$')) && trimmed.size() > 1)
        nodes = evalJsonPath(trimmed.mid(1), current, ctx);
    else if (trimmed.startsWith(QLatin1String("@css:")))
        nodes = evalCss(trimmed.mid(5).trimmed(), ctx, current);
    else if (trimmed.startsWith(QLatin1String("@XPath:")) || trimmed.startsWith(QLatin1String("//"))
             || trimmed.startsWith(QLatin1String("xpath:"))) {
        if (!warned_.contains(QStringLiteral("xpath"))) {
            warned_.insert(QStringLiteral("xpath"));
            Logger::instance().warn(QStringLiteral("[书源] 暂不支持 XPath 规则: %1")
                .arg(trimmed.left(80)));
        }
        return {};
    } else {
        nodes = evalDefault(trimmed, ctx, current);
    }

    if (!replaceSpec.isEmpty()) {
        const QList<QString> specParts = replaceSpec.split(QStringLiteral("##"));
        const QString pattern = specParts.value(0);
        const QString replacement = specParts.value(1);
        QRegularExpression re(pattern);
        if (re.isValid()) {
            for (RuleNode &node : nodes) {
                node = RuleNode::fromText(javaStyleReplace(node.asText(), re, replacement));
            }
        } else {
            Logger::instance().warn(QStringLiteral("[书源] ## 正则无效: %1").arg(pattern.left(80)));
        }
    }
    return nodes;
}

// ---- JS 评估 ----

QVector<RuleNode> RuleEngine::evalJs(const QString &code, const RuleContext &ctx,
                                     const RuleNode &current, const QString &tail) {
    const QJSValue value = evalJsValue(code, ctx, current);
    if (value.isError())
        return {};  // JS 执行失败（evalJsValue 已记录日志）
    QVector<RuleNode> nodes;

    if (value.isArray()) {
        const qint32 length = value.property(QStringLiteral("length")).toInt();
        for (qint32 i = 0; i < length; ++i) {
            const QJSValue item = value.property(i);
            if (item.isObject())
                nodes.append(RuleNode::fromJson(QJsonValue::fromVariant(item.toVariant())));
            else if (!item.isNull() && !item.isUndefined())
                nodes.append(RuleNode::fromText(item.toString()));
        }
    } else if (value.isObject()) {
        nodes.append(RuleNode::fromJson(QJsonValue::fromVariant(value.toVariant())));
    } else if (!value.isNull() && !value.isUndefined()) {
        nodes.append(RuleNode::fromText(value.toString()));
    }

    if (!tail.isEmpty()) {
        QVector<RuleNode> withTail;
        for (const RuleNode &node : nodes)
            withTail.append(evalRule(tail, ctx, node));
        return withTail;
    }
    return nodes;
}

QJSValue RuleEngine::evalJsValue(const QString &code, const RuleContext &ctx,
                                 const RuleNode &current) {
    prepareJsGlobals(ctx, current);
    variableProvider_ = ctx.getVariable;
    variableSetter_ = ctx.setVariable;
    currentKeyContext_ = ctx;
    loadJsLib(ctx.source);

    const QJSValue result = engine_.evaluate(code);
    if (result.isError())
        Logger::instance().warn(QStringLiteral("[书源] JS 执行失败: %1 | 代码: %2")
            .arg(result.toString().left(200), code.left(120).simplified()));
    return result;
}

QString RuleEngine::runJsString(const QString &code, const RuleContext &ctx,
                                const RuleNode &current) {
    const QJSValue value = evalJsValue(code, ctx, current);
    if (value.isNull() || value.isUndefined())
        return QString();
    if (value.isObject() && !value.isArray()) {
        const QJsonDocument doc = QJsonDocument::fromVariant(value.toVariant());
        if (!doc.isNull())
            return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    return value.toString();
}

void RuleEngine::prepareJsGlobals(const RuleContext &ctx, const RuleNode &current) {
    QJSValue global = engine_.globalObject();
    QJSValue resultValue;
    if (current.hasJson)
        resultValue = engine_.toScriptValue(current.json.toVariant());
    else
        resultValue = engine_.toScriptValue(current.asText());
    global.setProperty(QStringLiteral("result"), resultValue);
    global.setProperty(QStringLiteral("baseUrl"), engine_.toScriptValue(ctx.baseUrl));
    global.setProperty(QStringLiteral("key"), engine_.toScriptValue(ctx.key));
    global.setProperty(QStringLiteral("searchKey"), engine_.toScriptValue(ctx.key));
    global.setProperty(QStringLiteral("page"), engine_.toScriptValue(ctx.page));
    global.setProperty(QStringLiteral("searchPage"), engine_.toScriptValue(ctx.page));
    global.setProperty(QStringLiteral("title"), engine_.toScriptValue(ctx.title));

    QJSValue source = global.property(QStringLiteral("source"));
    if (source.isObject()) {
        source.setProperty(QStringLiteral("bookSourceUrl"),
                           engine_.toScriptValue(ctx.source ? ctx.source->url : QString()));
        source.setProperty(QStringLiteral("bookSourceName"),
                           engine_.toScriptValue(ctx.source ? ctx.source->name : QString()));
    }
}

// ---- JSONPath ----

QJsonValue RuleEngine::jsonRoot(const RuleNode &current, const RuleContext &ctx, bool *ok) const {
    *ok = false;
    const auto accept = [](const QJsonDocument &doc) -> QJsonValue {
        if (doc.isArray())
            return QJsonValue(doc.array());
        if (doc.isObject())
            return QJsonValue(doc.object());
        return QJsonValue();
    };
    if (current.hasJson) {
        *ok = true;
        return current.json;
    }
    const QString text = current.element ? current.element->allText() : current.text;
    if (!text.trimmed().isEmpty()) {
        const QJsonValue value = accept(QJsonDocument::fromJson(text.toUtf8()));
        if (!value.isNull()) {
            *ok = true;
            return value;
        }
    }
    if (!ctx.result.trimmed().isEmpty()) {
        const QJsonValue value = accept(QJsonDocument::fromJson(ctx.result.toUtf8()));
        if (!value.isNull()) {
            *ok = true;
            return value;
        }
    }
    return QJsonValue();
}

QVector<RuleNode> RuleEngine::evalJsonPath(const QString &path, const RuleNode &current,
                                           const RuleContext &ctx) {
    bool ok = false;
    const QJsonValue root = jsonRoot(current, ctx, &ok);
    if (!ok)
        return {};

    struct Step {
        enum Kind { Member, Index, Wildcard, DeepScan } kind = Member;
        QString name;
        int index = 0;
    };
    QVector<Step> steps;
    QString p = path.trimmed();
    if (p.startsWith(QLatin1Char('$')))
        p.remove(0, 1);

    qsizetype i = 0;
    QString pendingName;
    auto flushName = [&]() {
        if (!pendingName.isEmpty()) {
            steps.append({Step::Member, pendingName, 0});
            pendingName.clear();
        }
    };
    while (i < p.size()) {
        const QChar ch = p.at(i);
        if (ch == QLatin1Char('.')) {
            flushName();
            ++i;
            if (i < p.size() && p.at(i) == QLatin1Char('.')) {
                ++i;
                QString name;
                while (i < p.size() && p.at(i) != QLatin1Char('.') && p.at(i) != QLatin1Char('[')) {
                    name.append(p.at(i));
                    ++i;
                }
                if (!name.isEmpty())
                    steps.append({Step::DeepScan, name, 0});
            }
        } else if (ch == QLatin1Char('[')) {
            flushName();
            const int close = static_cast<int>(p.indexOf(QLatin1Char(']'), i));
            if (close < 0)
                break;
            const QString inner = p.mid(i + 1, close - i - 1).trimmed();
            i = close + 1;
            if (inner == QLatin1String("*")) {
                steps.append({Step::Wildcard, QString(), 0});
            } else if (inner.startsWith(QLatin1Char('\'')) || inner.startsWith(QLatin1Char('"'))) {
                if (inner.size() >= 2)
                    steps.append({Step::Member, inner.mid(1, inner.size() - 2), 0});
            } else {
                bool numOk = false;
                const int index = inner.toInt(&numOk);
                if (numOk)
                    steps.append({Step::Index, QString(), index});
            }
        } else {
            pendingName.append(ch);
            ++i;
        }
    }
    flushName();

    QJsonArray matches;
    matches.append(root);
    for (const Step &step : steps) {
        QJsonArray next;
        for (const QJsonValue &match : matches) {
            if (step.kind == Step::Member) {
                if (match.isObject()) {
                    const QJsonValue value = match.toObject().value(step.name);
                    if (!value.isUndefined())
                        next.append(value);
                }
            } else if (step.kind == Step::Index) {
                if (match.isArray()) {
                    const QJsonArray array = match.toArray();
                    int index = step.index;
                    if (index < 0)
                        index += array.size();
                    if (index >= 0 && index < array.size())
                        next.append(array.at(index));
                }
            } else if (step.kind == Step::Wildcard) {
                if (match.isArray()) {
                    for (const QJsonValue &item : match.toArray())
                        next.append(item);
                } else if (match.isObject()) {
                    const QJsonObject object = match.toObject();
                    for (auto it = object.begin(); it != object.end(); ++it)
                        next.append(it.value());
                }
            } else {  // DeepScan
                std::function<void(const QJsonValue &)> scan = [&](const QJsonValue &node) {
                    if (node.isObject()) {
                        const QJsonObject object = node.toObject();
                        const QJsonValue value = object.value(step.name);
                        if (!value.isUndefined())
                            next.append(value);
                        for (auto it = object.begin(); it != object.end(); ++it)
                            scan(it.value());
                    } else if (node.isArray()) {
                        for (const QJsonValue &item : node.toArray())
                            scan(item);
                    }
                };
                scan(match);
            }
        }
        matches = next;
        if (matches.isEmpty())
            return {};
    }

    QVector<RuleNode> nodes;
    if (matches.size() == 1 && matches.first().isArray()) {
        for (const QJsonValue &item : matches.first().toArray())
            nodes.append(RuleNode::fromJson(item));
        return nodes;
    }
    for (const QJsonValue &match : matches)
        nodes.append(RuleNode::fromJson(match));
    return nodes;
}

// ---- JSoup 默认查询 ----

QVector<RuleNode> RuleEngine::evalElements(const QList<HtmlNode::Ptr> &elements,
                                           const QString &extraction) {
    QVector<RuleNode> nodes;
    if (extraction.isEmpty()) {
        for (const HtmlNode::Ptr &element : elements)
            nodes.append(RuleNode::fromElement(element));
        return nodes;
    }
    for (const HtmlNode::Ptr &element : elements) {
        QString value;
        if (extraction == QLatin1String("text"))
            value = element->allText();
        else if (extraction == QLatin1String("textNodes"))
            value = element->textRuns.join(QStringLiteral("\n"));
        else if (extraction == QLatin1String("ownText"))
            value = element->ownText();
        else if (extraction == QLatin1String("html"))
            value = element->innerHtml();
        else if (extraction == QLatin1String("all"))
            value = element->outerHtml();
        else
            value = element->attr(extraction);
        nodes.append(RuleNode::fromText(value));
    }
    return nodes;
}

QVector<RuleNode> RuleEngine::evalDefault(const QString &rule, const RuleContext &ctx,
                                          const RuleNode &current) {
    QList<HtmlNode::Ptr> elements;
    if (current.element) {
        elements.append(current.element);
    } else {
        const QString html = current.text.isEmpty() ? ctx.result : current.text;
        elements.append(HtmlDom::parse(html));
    }
    if (elements.isEmpty())
        return {};

    const QStringList tokens = rule.split(QLatin1Char('@'));
    for (int ti = 0; ti < tokens.size(); ++ti) {
        const QString token = tokens.at(ti).trimmed();
        if (token.isEmpty())
            continue;

        // [n] / [n:m] 索引
        if (token.startsWith(QLatin1Char('['))) {
            static const QRegularExpression indexRe(
                QStringLiteral(R"(^\[(-?\d+)(?::(-?\d+))?\]$)"));
            const QRegularExpressionMatch m = indexRe.match(token);
            if (!m.hasMatch())
                continue;
            const int from = m.captured(1).toInt();
            const bool hasRange = !m.captured(2).isEmpty();
            const int to = hasRange ? m.captured(2).toInt() : from;
            QList<HtmlNode::Ptr> selected;
            const int size = elements.size();
            auto normalize = [size](int v) { return v < 0 ? size + v : v; };
            const int f = qBound(0, normalize(from), size - 1);
            const int t = qBound(0, normalize(to), size - 1);
            for (int idx = f; idx <= t && idx < size; ++idx)
                selected.append(elements.at(idx));
            elements = selected;
            continue;
        }

        if (token == QLatin1String("children")) {
            QList<HtmlNode::Ptr> children;
            for (const HtmlNode::Ptr &element : elements) {
                for (const HtmlNode::Ptr &child : element->children)
                    if (child->isElement())
                        children.append(child);
            }
            elements = children;
            continue;
        }

        // 终结提取关键字
        if (kExtractionKeywords.contains(token))
            return evalElements(elements, token);

        // 选择器前缀
        if (token.startsWith(QLatin1String("class."))) {
            const QString className = token.mid(6);
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements)
                next.append(element->descendantsByClass(className));
            elements = next;
            continue;
        }
        if (token.startsWith(QLatin1Char('.')) && token.size() > 1) {
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements)
                next.append(element->descendantsByClass(token.mid(1)));
            elements = next;
            continue;
        }
        if (token.startsWith(QLatin1String("id."))) {
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements)
                next.append(element->descendantsById(token.mid(3)));
            elements = next;
            continue;
        }
        if (token.startsWith(QLatin1Char('#')) && token.size() > 1) {
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements)
                next.append(element->descendantsById(token.mid(1)));
            elements = next;
            continue;
        }
        if (token.startsWith(QLatin1String("tag."))) {
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements)
                next.append(element->descendantsByTag(token.mid(4)));
            elements = next;
            continue;
        }
        if (token.startsWith(QLatin1String("text."))) {
            const QString needle = token.mid(5);
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements) {
                for (const HtmlNode::Ptr &found : element->descendantsByTag(QStringLiteral("*"))) {
                    if (found->ownText().contains(needle))
                        next.append(found);
                }
            }
            elements = next;
            continue;
        }
        if (kCommonTags.contains(token.toLower())) {
            QList<HtmlNode::Ptr> next;
            for (const HtmlNode::Ptr &element : elements)
                next.append(element->descendantsByTag(token.toLower()));
            elements = next;
            continue;
        }

        // 其余按属性名提取（href/src/data-*/content 等）
        return evalElements(elements, token);
    }

    return evalElements(elements, QString());
}

// ---- 简化 CSS ----

QVector<RuleNode> RuleEngine::evalCss(const QString &rule, const RuleContext &ctx,
                                      const RuleNode &current) {
    const int attrPos = static_cast<int>(rule.indexOf(QLatin1Char('@')));
    const QString selector = attrPos >= 0 ? rule.left(attrPos).trimmed() : rule.trimmed();
    const QString extraction = attrPos >= 0 ? rule.mid(attrPos + 1).trimmed()
                                            : QStringLiteral("text");

    QList<HtmlNode::Ptr> roots;
    if (current.element) {
        roots.append(current.element);
    } else {
        roots.append(HtmlDom::parse(current.text.isEmpty() ? ctx.result : current.text));
    }
    if (roots.isEmpty() || selector.isEmpty())
        return {};

    // 解析为带直接子级标记的选择器链：A > B 时 B.child = true
    struct Part {
        QString simple;
        bool child = false;
    };
    QVector<Part> chain;
    QString currentSimple;
    bool pendingChild = false;
    for (qsizetype i = 0; i < selector.size(); ++i) {
        const QChar ch = selector.at(i);
        if (ch == QLatin1Char(' ')) {
            if (!currentSimple.isEmpty()) {
                chain.append({currentSimple, pendingChild});
                currentSimple.clear();
                pendingChild = false;
            }
        } else if (ch == QLatin1Char('>')) {
            if (!currentSimple.isEmpty()) {
                chain.append({currentSimple, pendingChild});
                currentSimple.clear();
            }
            pendingChild = true;
        } else {
            currentSimple.append(ch);
        }
    }
    if (!currentSimple.isEmpty())
        chain.append({currentSimple, pendingChild});
    if (chain.isEmpty())
        return {};

    std::function<bool(HtmlNode *, int)> matchFrom =
        [&](HtmlNode *node, int partIndex) -> bool {
        if (!node || !node->matchesSimple(chain[partIndex].simple))
            return false;
        if (partIndex == 0)
            return true;
        for (HtmlNode *parent = node->parent; parent; parent = parent->parent) {
            if (parent->tag == QLatin1String("#document"))
                break;
            if (matchFrom(parent, partIndex - 1))
                return true;
            if (chain[partIndex].child)
                break;
        }
        return false;
    };

    QList<HtmlNode::Ptr> matched;
    for (const HtmlNode::Ptr &root : roots) {
        for (const HtmlNode::Ptr &candidate : root->descendantsByTag(QStringLiteral("*"))) {
            if (matchFrom(candidate.get(), static_cast<int>(chain.size()) - 1))
                matched.append(candidate);
        }
    }
    return evalElements(matched, extraction);
}
