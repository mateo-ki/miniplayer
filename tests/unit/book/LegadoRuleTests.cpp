#include <QtTest/QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "book/BookSource.h"
#include "book/HtmlDom.h"
#include "book/LegadoClient.h"
#include "book/RuleEngine.h"
#include "models/LegadoBookModel.h"
#include "models/LegadoSourceModel.h"

// 书源规则引擎单元测试：JSONPath / 默认查询 / JS / 模板 / URL 选项。
// 另含可选的端到端测试：设置 MELOBOX_LEGADO_E2E=1 并用 MELOBOX_LEGADO_JSON
// 指向书源合集 JSON（如 yckceo 1248.json）时，走真实网络的
// 导入 → 选择源 → 搜索 → 详情 → 目录 → 正文流程。
class LegadoRuleTests final : public QObject {
    Q_OBJECT

private slots:
    void jsonPathBasic();
    void jsonPathWildcard();
    void jsonPathDeepScan();
    void htmlDefaultQuery();
    void htmlIndexAndChildren();
    void cssQuery();
    void regexReplace();
    void fallbackAndMerge();
    void jsRule();
    void chainedJs();
    void urlTemplate();
    void urlPostOptions();
    void sourceJsonRoundtrip();
    void entityDecoding();
    void tocListRule();
    void legadoEndToEnd();

private:
    RuleContext makeContext(const QString &result, const QString &baseUrl = {}) const {
        RuleContext ctx;
        ctx.result = result;
        ctx.baseUrl = baseUrl;
        ctx.key = QStringLiteral("斗罗大陆");
        ctx.page = 2;
        return ctx;
    }
};

void LegadoRuleTests::jsonPathBasic() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(
        R"({"a":{"b":"hello"},"n":42})");

    QCOMPARE(engine.evaluateString("$.a.b", ctx, RuleNode::fromText(ctx.result)),
             QStringLiteral("hello"));
    QCOMPARE(engine.evaluateString("$.n", ctx, RuleNode::fromText(ctx.result)),
             QStringLiteral("42"));

    // 数组单元素自动展开
    const RuleContext arrayCtx = makeContext(
        R"({"list":[{"name":"第一本"},{"name":"第二本"}]})");
    QCOMPARE(engine.evaluateString("$.list[0].name", arrayCtx, RuleNode::fromText(arrayCtx.result)),
             QStringLiteral("第一本"));
    QCOMPARE(engine.evaluateString("$.list[-1].name", arrayCtx, RuleNode::fromText(arrayCtx.result)),
             QStringLiteral("第二本"));
}

void LegadoRuleTests::jsonPathWildcard() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(
        R"({"data":[{"name":"甲","id":1},{"name":"乙","id":2}]})");

    const QVector<RuleNode> nodes = engine.evaluateList("$.data[*]", ctx);
    QCOMPARE(nodes.size(), 2);
    QCOMPARE(nodes.at(0).asText().contains(QStringLiteral("甲")), true);
    QCOMPARE(engine.evaluateStrings("$.data[*].id", ctx, RuleNode::fromText(ctx.result))
                 .join(QStringLiteral(",")),
             QStringLiteral("1,2"));
}

void LegadoRuleTests::jsonPathDeepScan() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(
        R"({"a":{"items":[{"cover":"u1"},{"cover":"u2"}]}})");
    const QStringList covers = engine.evaluateStrings("$..cover", ctx,
        RuleNode::fromText(ctx.result));
    QCOMPARE(covers.size(), 2);
    QCOMPARE(covers.at(1), QStringLiteral("u2"));
}

void LegadoRuleTests::htmlDefaultQuery() {
    LegadoClient client;
    RuleEngine engine(&client);
    const QString html = QStringLiteral(
        "<html><body>"
        "<div class='bookbox' id='b1'><h4><a href='/book/1'>书名一</a></h4>"
        "<span class='author'>作者甲</span></div>"
        "<div class='bookbox' id='b2'><h4><a href='/book/2'>书名二</a></h4>"
        "<span class='author'>作者乙</span></div>"
        "</body></html>");
    const RuleContext ctx = makeContext(html);

    const QVector<RuleNode> boxes = engine.evaluateList(QStringLiteral("class.bookbox"), ctx);
    QCOMPARE(boxes.size(), 2);

    const QStringList names = engine.evaluateStrings(
        QStringLiteral("class.bookbox@tag.h4@tag.a@text"), ctx, RuleNode::fromText(html));
    QCOMPARE(names.size(), 2);
    QCOMPARE(names.at(0), QStringLiteral("书名一"));

    const QStringList hrefs = engine.evaluateStrings(
        QStringLiteral("class.bookbox@tag.a@href"), ctx, RuleNode::fromText(html));
    QCOMPARE(hrefs.size(), 2);
    QCOMPARE(hrefs.at(1), QStringLiteral("/book/2"));

    QCOMPARE(engine.evaluateStrings(QStringLiteral("#b1@tag.span@text"), ctx,
                RuleNode::fromText(html)).join(QString()),
             QStringLiteral("作者甲"));
    QCOMPARE(engine.evaluateStrings(QStringLiteral("id.b2@text"), ctx,
                RuleNode::fromText(html)).join(QString()).contains(QStringLiteral("书名二")),
             true);
}

void LegadoRuleTests::htmlIndexAndChildren() {
    LegadoClient client;
    RuleEngine engine(&client);
    const QString html = QStringLiteral(
        "<ul id='list'><li>一</li><li>二</li><li>三</li></ul>");
    const RuleContext ctx = makeContext(html);

    QCOMPARE(engine.evaluateStrings(QStringLiteral("id.list@tag.li@[0]@text"), ctx,
                RuleNode::fromText(html)).join(QString()),
             QStringLiteral("一"));
    QCOMPARE(engine.evaluateStrings(QStringLiteral("id.list@tag.li@[-1]@text"), ctx,
                RuleNode::fromText(html)).join(QString()),
             QStringLiteral("三"));
    // children 取直接子级
    QCOMPARE(engine.evaluateStrings(QStringLiteral("id.list@children@text"), ctx,
                RuleNode::fromText(html)).size(), 3);
}

void LegadoRuleTests::cssQuery() {
    LegadoClient client;
    RuleEngine engine(&client);
    const QString html = QStringLiteral(
        "<div class='wrap'><a class='t' href='/a'>链接A</a>"
        "<div><a class='t' href='/b'>链接B</a></div></div>");
    const RuleContext ctx = makeContext(html);

    const QStringList texts = engine.evaluateStrings(
        QStringLiteral("@css:a.t@text"), ctx, RuleNode::fromText(html));
    QCOMPARE(texts.size(), 2);
    QCOMPARE(texts.at(0), QStringLiteral("链接A"));

    const QStringList hrefs = engine.evaluateStrings(
        QStringLiteral("@css:div.wrap > div > a@href"), ctx, RuleNode::fromText(html));
    QCOMPARE(hrefs.size(), 1);
    QCOMPARE(hrefs.at(0), QStringLiteral("/b"));
}

void LegadoRuleTests::regexReplace() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(QStringLiteral(R"js({"name":"第12章 相遇 (修)"})js"));
    QCOMPARE(engine.evaluateStrings(QStringLiteral("$.name##\\s*\\(修\\)"), ctx,
                RuleNode::fromText(ctx.result)).join(QString()),
             QStringLiteral("第12章 相遇"));
    QCOMPARE(engine.evaluateStrings(QStringLiteral("$.name##第(\\d+)章##$1章"), ctx,
                RuleNode::fromText(ctx.result)).join(QString()),
             QStringLiteral("12章 相遇 (修)"));
}

void LegadoRuleTests::fallbackAndMerge() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(R"({"present":"ok","a":"1","b":"2"})");

    // || 取第一个非空分支
    QCOMPARE(engine.evaluateStrings(QStringLiteral("$.missing||$.present"), ctx,
                RuleNode::fromText(ctx.result)).join(QString()),
             QStringLiteral("ok"));
    // && 合并（评估结果为多条，测试以 \n 连接核对）
    QCOMPARE(engine.evaluateStrings(QStringLiteral("$.a&&$.b"), ctx,
                RuleNode::fromText(ctx.result)).join(QStringLiteral("\n")),
             QStringLiteral("1\n2"));
}

void LegadoRuleTests::jsRule() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(R"("raw")");

    // ctx.result 本身是 JSON 字符串文档（含引号），JS 中 result 原样带入
    QCOMPARE(engine.evaluateStrings(QStringLiteral("@js:result + '!'"), ctx,
                RuleNode::fromText(ctx.result)).join(QString()),
             QStringLiteral("\"raw\"!"));
    // key/page 变量注入
    QCOMPARE(engine.evaluateStrings(QStringLiteral("@js:key + '-' + page"), ctx,
                RuleNode::fromText(ctx.result)).join(QString()),
             QStringLiteral("斗罗大陆-2"));
    // JS 输出 JSON 对象 → 列表
    const QVector<RuleNode> nodes = engine.evaluateList(
        QStringLiteral("@js:[{name:'a'},{name:'b'}]"), ctx);
    QCOMPARE(nodes.size(), 2);
}

void LegadoRuleTests::chainedJs() {
    LegadoClient client;
    RuleEngine engine(&client);
    const RuleContext ctx = makeContext(R"({"book_id": 77})");
    QCOMPARE(engine.evaluateStrings(QStringLiteral("$.book_id@js:'https://x/api?id=' + result"), ctx,
                RuleNode::fromText(ctx.result)).join(QString()),
             QStringLiteral("https://x/api?id=77"));
}

void LegadoRuleTests::urlTemplate() {
    LegadoClient client;
    const QString url = client.resolveUrlRule(
        QStringLiteral("https://a.com/s?q={{key}}&p={{page}}"),
        QStringLiteral("https://a.com"), QStringLiteral("关键词"), 3, nullptr);
    // 关键字保持原文，由 QUrl 在请求时统一编码
    QCOMPARE(url, QStringLiteral("https://a.com/s?q=%1&p=3").arg(QStringLiteral("关键词")));

    // {{表达式}} 走 JS
    const QString offset = client.resolveUrlRule(
        QStringLiteral("https://a.com/list/{{(page-1)*20}}"),
        QStringLiteral("https://a.com"), QStringLiteral("k"), 2, nullptr);
    QCOMPARE(offset, QStringLiteral("https://a.com/list/20"));
}

void LegadoRuleTests::urlPostOptions() {
    LegadoClient client;
    LegadoFetchOptions options;
    const QString url = client.resolveUrlRule(
        QStringLiteral("https://a.com/s,{\"method\":\"POST\",\"body\":\"k={{key}}&p={{page}}\","
                       "\"charset\":\"gbk\",\"headers\":{\"Referer\":\"https://a.com\"}}"),
        QStringLiteral("https://a.com"), QStringLiteral("词"), 5, &options);
    QCOMPARE(url, QStringLiteral("https://a.com/s"));
    QCOMPARE(options.method, QStringLiteral("POST"));
    QCOMPARE(options.body, QStringLiteral("k=%1&p=5").arg(QStringLiteral("词")));
    QCOMPARE(options.charset, QStringLiteral("gbk"));
    QCOMPARE(options.headers.value(QStringLiteral("Referer")), QStringLiteral("https://a.com"));
}

void LegadoRuleTests::sourceJsonRoundtrip() {
    const QJsonDocument doc = QJsonDocument::fromJson(QString(
        R"json({"bookSourceName":"测试源","bookSourceUrl":"https://src.example","bookSourceType":1,
            "bookSourceGroup":"测试组","enabled":true,"enabledExplore":true,
            "searchUrl":"/search?q={{key}}",
            "jsLib":"function T(){return 1;}",
            "ruleSearch":{"bookList":"$.list","name":"$.name","bookUrl":"$.url"},
            "ruleToc":{"chapterList":"class.list","chapterName":"text","chapterUrl":"href"},
            "ruleContent":{"content":"id.content@textNodes"}})json").toUtf8());
    QVERIFY(doc.isObject());

    const BookSource source = BookSource::fromJson(doc.object());
    QCOMPARE(source.name, QStringLiteral("测试源"));
    QCOMPARE(source.url, QStringLiteral("https://src.example"));
    QCOMPARE(source.type, 1);
    QCOMPARE(source.ruleSearch.value(QStringLiteral("bookList")).toString(),
             QStringLiteral("$.list"));
    QCOMPARE(source.ruleContent.value(QStringLiteral("content")).toString(),
             QStringLiteral("id.content@textNodes"));

    const BookSource reparsed = BookSource::fromJson(
        QJsonDocument(source.toJson()).object());
    QCOMPARE(reparsed.name, source.name);
    QCOMPARE(reparsed.ruleToc.value(QStringLiteral("chapterUrl")).toString(),
             QStringLiteral("href"));
}

void LegadoRuleTests::entityDecoding() {
    QCOMPARE(HtmlDom::decodeEntities(QStringLiteral("a&amp;b&nbsp;c&#65;&#x42;")),
             QStringLiteral("a&b cAB"));
    auto doc = HtmlDom::parse(QStringLiteral("<p title='x&amp;y'>t1<br/>t2</p>"));
    QCOMPARE(doc->children.first()->attr(QStringLiteral("title")), QStringLiteral("x&y"));
    QCOMPARE(doc->children.first()->allText().contains(QStringLiteral("t1")), true);
    QCOMPARE(doc->children.first()->allText().contains(QStringLiteral("t2")), true);
}

void LegadoRuleTests::tocListRule() {
    LegadoClient client;
    RuleEngine engine(&client);
    const QString html = QStringLiteral(
        "<div class='list'><li><a href='/c1'>章节一</a></li>"
        "<li><a href='/c2'>章节二</a></li></div>");
    const RuleContext ctx = makeContext(html);

    const QVector<RuleNode> items = engine.evaluateList(QStringLiteral("class.list@tag.li"), ctx);
    QCOMPARE(items.size(), 2);
    const QStringList titles = engine.evaluateStrings(QStringLiteral("class.list@tag.a@text"),
        ctx, RuleNode::fromText(html));
    QCOMPARE(titles.at(0), QStringLiteral("章节一"));
    QCOMPARE(titles.at(1), QStringLiteral("章节二"));
}

void LegadoRuleTests::legadoEndToEnd() {
    if (!qEnvironmentVariableIsSet("MELOBOX_LEGADO_E2E"))
        QSKIP("设置 MELOBOX_LEGADO_E2E=1 与 MELOBOX_LEGADO_JSON=<书源合集路径> 后运行");

    const QString jsonPath = qEnvironmentVariable("MELOBOX_LEGADO_JSON");
    QFile file(jsonPath);
    QVERIFY2(file.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("无法打开书源文件: %1").arg(jsonPath)));

    LegadoSourceModel sourceModel;
    sourceModel.importFromText(QString::fromUtf8(file.readAll()));
    qDebug() << "导入书源数:" << sourceModel.count();
    QVERIFY(sourceModel.count() > 0);

    // 优先选番茄聚合源（@js: + JSONPath 组合的代表性书源），否则选第一个文本源
    int row = -1;
    for (int i = 0; i < sourceModel.count(); ++i) {
        if (sourceModel.sourceAt(i).name.contains(QStringLiteral("番茄"))) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        for (int i = 0; i < sourceModel.count(); ++i) {
            if (sourceModel.sourceAt(i).type == 0) {
                row = i;
                break;
            }
        }
    }
    QVERIFY(row >= 0);
    qDebug() << "选中书源:" << sourceModel.sourceAt(row).name;
    sourceModel.selectSource(row);

    LegadoBookModel bookModel;
    bookModel.setSourceModel(&sourceModel);
    bookModel.applyActiveSource();
    QVERIFY(bookModel.hasSource());

    // 上游 API 失效在书源圈很常见（合集里不少源已 502/超时），
    // 依次尝试前若干个文本源直到某个源搜索成功——与真实使用方式一致。
    const int kMaxTries = 20;
    int tried = 0;
    for (int i = 0; i < sourceModel.count() && bookModel.count() == 0 && tried < kMaxTries; ++i) {
        const BookSource candidate = sourceModel.sourceAt(i);
        if (candidate.type != 0 || !candidate.enabled
            || candidate.searchUrl.trimmed().isEmpty())
            continue;
        ++tried;
        sourceModel.selectSource(i);
        bookModel.applyActiveSource();
        bookModel.search(QStringLiteral("我的"));
        qDebug() << "尝试源" << tried << ":" << candidate.name
                 << "结果数:" << bookModel.count()
                 << "message:" << bookModel.message();
        // 有些源只有搜索/详情没有目录规则，跳过以保证全链路验证
        if (bookModel.count() > 0 && candidate.ruleToc.isEmpty())
            bookModel.clearResults();
    }
    QVERIFY2(bookModel.count() > 0,
             qPrintable(QStringLiteral("前 %1 个源均搜索失败").arg(tried)));
    qDebug() << "第一条 bookUrl:"
             << bookModel.data(bookModel.index(0, 0), LegadoBookModel::BookUrlRole).toString()
             << "cover:" << bookModel.data(bookModel.index(0, 0), LegadoBookModel::CoverRole).toString();

    bookModel.loadDetail(0);
    qDebug() << "详情书名:" << bookModel.detail().value("name").toString();
    QVERIFY(bookModel.hasDetail());

    bookModel.loadToc();
    qDebug() << "章节数:" << bookModel.chapterCount() << "message:" << bookModel.message();
    QVERIFY2(bookModel.chapterCount() > 0,
             qPrintable(QStringLiteral("目录为空: %1").arg(bookModel.message())));

    bookModel.loadContent(0);
    qDebug() << "正文长度:" << bookModel.content().size()
             << "message:" << bookModel.message();
    QVERIFY2(!bookModel.content().isEmpty(),
             qPrintable(QStringLiteral("正文为空: %1").arg(bookModel.message())));
    qDebug() << "正文预览:" << bookModel.content().left(80);
}

QTEST_GUILESS_MAIN(LegadoRuleTests)
#include "LegadoRuleTests.moc"
