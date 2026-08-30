#include "book/HtmlDom.h"

#include <QRegularExpression>
#include <QSet>
#include <functional>

#include <QHash>

namespace {

const QSet<QString> &voidElements() {
    static const QSet<QString> elements = {
        QStringLiteral("area"), QStringLiteral("base"), QStringLiteral("br"),
        QStringLiteral("col"), QStringLiteral("embed"), QStringLiteral("hr"),
        QStringLiteral("img"), QStringLiteral("input"), QStringLiteral("link"),
        QStringLiteral("meta"), QStringLiteral("param"), QStringLiteral("source"),
        QStringLiteral("track"), QStringLiteral("wbr")
    };
    return elements;
}

// 这些标签的内容是纯文本，内部不再解析子标签。
const QSet<QString> &rawTextElements() {
    static const QSet<QString> elements = {
        QStringLiteral("script"), QStringLiteral("style"),
        QStringLiteral("textarea"), QStringLiteral("title")
    };
    return elements;
}

void collectDescendants(const HtmlNode::Ptr &node,
                        const std::function<bool(const HtmlNode::Ptr &)> &predicate,
                        QList<HtmlNode::Ptr> &out) {
    for (const HtmlNode::Ptr &child : node->children) {
        if (!child->isElement())
            continue;
        if (predicate(child))
            out.append(child);
        collectDescendants(child, predicate, out);
    }
}

void appendHtml(const HtmlNode::Ptr &node, QString *out) {
    if (!node->isElement()) {
        out->append(HtmlDom::decodeEntities(node->textRuns.join(QString())));
        return;
    }
    out->append(QLatin1Char('<'));
    out->append(node->tag);
    for (auto it = node->attrs.constBegin(); it != node->attrs.constEnd(); ++it) {
        out->append(QStringLiteral(" %1=\"%2\"").arg(it.key(),
            QString(it.value()).replace(QLatin1Char('"'), QStringLiteral("&quot;"))));
    }
    out->append(QLatin1Char('>'));
    for (const HtmlNode::Ptr &child : node->children)
        appendHtml(child, out);
    out->append(QStringLiteral("</%1>").arg(node->tag));
}

}  // namespace

QString HtmlNode::attr(const QString &name) const {
    return attrs.value(name.toLower());
}

QString HtmlNode::ownText() const {
    return textRuns.join(QStringLiteral(" ")).simplified();
}

QString HtmlNode::allText() const {
    QStringList pieces;
    pieces.append(textRuns);
    std::function<void(const HtmlNode::Ptr &)> walk = [&](const HtmlNode::Ptr &child) {
        for (const HtmlNode::Ptr &grand : child->children) {
            if (grand->isElement()) {
                pieces.append(grand->textRuns);
                walk(grand);
            }
        }
    };
    for (const HtmlNode::Ptr &child : children) {
        if (child->isElement()) {
            pieces.append(child->textRuns);
            walk(child);
        }
    }
    QString joined = pieces.join(QLatin1Char(' '));
    joined.replace(QStringLiteral("\n\n\n\n"), QStringLiteral("\n\n"));
    return joined.trimmed();
}

QString HtmlNode::outerHtml() {
    QString html;
    appendHtml(shared_from_this(), &html);
    return html;
}

QString HtmlNode::innerHtml() const {
    QString html;
    for (const HtmlNode::Ptr &child : children)
        appendHtml(child, &html);
    return html;
}

QList<HtmlNode::Ptr> HtmlNode::descendantsByTag(const QString &tagName) {
    QList<HtmlNode::Ptr> out;
    collectDescendants(shared_from_this(), [&](const HtmlNode::Ptr &n) {
        return tagName == QLatin1String("*") || n->tag == tagName;
    }, out);
    return out;
}

QList<HtmlNode::Ptr> HtmlNode::descendantsByClass(const QString &className) {
    QList<HtmlNode::Ptr> out;
    const QString wanted = className.trimmed();
    collectDescendants(shared_from_this(), [&](const HtmlNode::Ptr &n) {
        return n->attrs.value(QStringLiteral("class"))
            .split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(wanted);
    }, out);
    return out;
}

QList<HtmlNode::Ptr> HtmlNode::descendantsById(const QString &id) {
    QList<HtmlNode::Ptr> out;
    collectDescendants(shared_from_this(), [&](const HtmlNode::Ptr &n) {
        return n->attrs.value(QStringLiteral("id")) == id;
    }, out);
    return out;
}

bool HtmlNode::matchesSimple(const QString &simple) const {
    // 形如 div.x.y#z[k=v] 的组合简单选择器
    QString tag = QStringLiteral("*");
    QStringList classes;
    QString id;
    QList<QPair<QString, QString>> attrChecks;

    qsizetype i = 0;
    QString token;
    while (i < simple.size()) {
        const QChar ch = simple.at(i);
        if (ch == QLatin1Char('.') || ch == QLatin1Char('#') || ch == QLatin1Char('[')) {
            if (!token.isEmpty()) {
                if (tag == QLatin1String("*"))
                    tag = token;
                token.clear();
            }
            if (ch == QLatin1Char('.')) {
                token.clear();
                ++i;
                while (i < simple.size() && simple.at(i) != QLatin1Char('.')
                       && simple.at(i) != QLatin1Char('#') && simple.at(i) != QLatin1Char('['))
                    token.append(simple.at(i++));
                classes.append(token);
                token.clear();
            } else if (ch == QLatin1Char('#')) {
                token.clear();
                ++i;
                while (i < simple.size() && simple.at(i) != QLatin1Char('.')
                       && simple.at(i) != QLatin1Char('#') && simple.at(i) != QLatin1Char('['))
                    token.append(simple.at(i++));
                id = token;
                token.clear();
            } else {
                ++i;
                QString inner;
                while (i < simple.size() && simple.at(i) != QLatin1Char(']'))
                    inner.append(simple.at(i++));
                ++i;
                const int eq = inner.indexOf(QLatin1Char('='));
                if (eq >= 0)
                    attrChecks.append({inner.left(eq).trimmed(), inner.mid(eq + 1).trimmed()});
                else
                    attrChecks.append({inner.trimmed(), QString()});
            }
        } else {
            token.append(ch);
            ++i;
        }
    }
    if (!token.isEmpty() && tag == QLatin1String("*"))
        tag = token;

    if (tag != QLatin1String("*") && tag.compare(this->tag, Qt::CaseInsensitive) != 0)
        return false;
    if (!id.isNull() && attrs.value(QStringLiteral("id")) != id)
        return false;
    for (const QString &cls : classes) {
        if (!attrs.value(QStringLiteral("class"))
                .split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(cls))
            return false;
    }
    for (const auto &check : attrChecks) {
        const QString value = attrs.value(check.first);
        if (check.second.isEmpty()) {
            if (!attrs.contains(check.first))
                return false;
        } else if (value != check.second) {
            return false;
        }
    }
    return true;
}

QString HtmlDom::decodeEntities(QString text) {
    static const QHash<QString, QChar> named = {
        {QStringLiteral("amp"), QLatin1Char('&')}, {QStringLiteral("lt"), QLatin1Char('<')},
        {QStringLiteral("gt"), QLatin1Char('>')}, {QStringLiteral("quot"), QLatin1Char('"')},
        {QStringLiteral("apos"), QLatin1Char('\'')}, {QStringLiteral("nbsp"), QLatin1Char(' ')}
    };
    if (!text.contains(QLatin1Char('&')))
        return text;
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('&')) {
            out.append(text.at(i));
            continue;
        }
        const int semi = text.indexOf(QLatin1Char(';'), i + 1);
        if (semi < 0 || semi - i > 12) {
            out.append(QLatin1Char('&'));
            continue;
        }
        const QString body = text.mid(i + 1, semi - i - 1);
        if (body.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            int code = 0;
            if (body.size() > 1 && (body.at(1) == QLatin1Char('x') || body.at(1) == QLatin1Char('X')))
                code = body.mid(2).toInt(&ok, 16);
            else
                code = body.mid(1).toInt(&ok, 10);
            out.append(ok && code > 0 ? QChar(code) : QLatin1Char('&'));
        } else if (named.contains(body)) {
            out.append(named.value(body));
        } else {
            out.append(QLatin1Char('&')).append(body).append(QLatin1Char(';'));
            i = semi;  // 已附加整个实体，跳过
            continue;
        }
        i = semi;
    }
    return out;
}

HtmlNode::Ptr HtmlDom::parse(const QString &html) {
    auto root = std::make_shared<HtmlNode>();
    root->tag = QStringLiteral("#document");
    HtmlNode *current = root.get();

    const qsizetype n = html.size();
    qsizetype i = 0;
    while (i < n) {
        const int lt = html.indexOf(QLatin1Char('<'), i);
        if (lt < 0) {
            current->textRuns.append(HtmlDom::decodeEntities(html.mid(i).trimmed()));
            break;
        }
        if (lt > i) {
            QString text = html.mid(i, lt - i);
            // 标签之间的空白按单词分隔聚合，避免每段文本变成一个 run
            text = text.simplified();
            if (!text.isEmpty())
                current->textRuns.append(HtmlDom::decodeEntities(text));
        }

        if (html.indexOf(QLatin1String("<!--"), lt) == lt) {
            const int end = html.indexOf(QLatin1String("-->"), lt + 4);
            i = end < 0 ? n : end + 3;
            continue;
        }
        if (html.indexOf(QLatin1String("<!"), lt) == lt || html.indexOf(QLatin1String("<?"), lt) == lt) {
            const int end = html.indexOf(QLatin1Char('>'), lt);
            i = end < 0 ? n : end + 1;
            continue;
        }

        const int gt = html.indexOf(QLatin1Char('>'), lt);
        if (gt < 0)
            break;
        QString inside = html.mid(lt + 1, gt - lt - 1).trimmed();
        i = gt + 1;

        if (inside.startsWith(QLatin1Char('/'))) {
            const QString closing = inside.mid(1).split(QLatin1Char(' ')).value(0).toLower();
            // 回溯到最近的同名开标签；找不到就忽略
            HtmlNode *node = current;
            while (node && node->tag != closing)
                node = node->parent;
            if (node && node->parent)
                current = node->parent;
            continue;
        }

        // 标签名与属性
        static const QRegularExpression tagRe(QLatin1String("^([A-Za-z][A-Za-z0-9:-]*)"));
        const QRegularExpressionMatch tagMatch = tagRe.match(inside);
        if (!tagMatch.hasMatch())
            continue;
        const QString tagName = tagMatch.captured(1).toLower();
        QString attrText = inside.mid(tagMatch.capturedLength(1));

        auto node = std::make_shared<HtmlNode>();
        node->tag = tagName;
        node->parent = current;

        static const QRegularExpression attrRe(
            QLatin1String("([A-Za-z_:@][A-Za-z0-9_:.@-]*)(?:\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s\"'>]+))?"));
        auto it = attrRe.globalMatch(attrText);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString value = m.captured(2);
            if (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
                value = value.mid(1, value.size() - 2);
            node->attrs.insert(m.captured(1).toLower(), HtmlDom::decodeEntities(value));
        }

        current->children.append(node);

        const bool selfClosed = inside.endsWith(QLatin1Char('/'));
        if (rawTextElements().contains(tagName)) {
            const int closeIdx = html.indexOf(QLatin1String("</"), i);
            if (closeIdx > 0) {
                const QString raw = html.mid(i, closeIdx - i);
                if (tagName != QLatin1String("textarea") && tagName != QLatin1String("title"))
                    node->textRuns.append(raw);  // script/style 保留原文，避免实体解码破坏 JS
                else
                    node->textRuns.append(HtmlDom::decodeEntities(raw));
                const int closeGt = html.indexOf(QLatin1Char('>'), closeIdx);
                i = closeGt < 0 ? n : closeGt + 1;
            }
        } else if (!selfClosed && !voidElements().contains(tagName)) {
            current = node.get();
        }
    }
    return root;
}
