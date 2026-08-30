#include "models/LegadoBookModel.h"

#include <QRegularExpression>
#include <QUrl>

#include "infrastructure/Logger.h"
#include "models/LegadoSourceModel.h"

namespace {

constexpr int kMaxNextContentHops = 10;
constexpr int kMaxNextTocHops = 40;

QString ruleField(const QVariantMap &rules, const char *key) {
    return rules.value(QLatin1String(key)).toString();
}

}  // namespace

LegadoBookModel::LegadoBookModel(QObject *parent)
    : QAbstractListModel(parent), client_(this) {
    engine_ = client_.engine();
}

int LegadoBookModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : items_.size();
}

QVariant LegadoBookModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size())
        return {};
    const SearchItem &item = items_.at(index.row());
    switch (role) {
    case NameRole: return item.name;
    case AuthorRole: return item.author;
    case CoverRole: return item.coverUrl;
    case IntroRole: return item.intro;
    case KindRole: return item.kind;
    case LastChapterRole: return item.lastChapter;
    case BookUrlRole: return item.bookUrl;
    default: return {};
    }
}

QHash<int, QByteArray> LegadoBookModel::roleNames() const {
    return {
        {NameRole, "name"},
        {AuthorRole, "author"},
        {CoverRole, "cover"},
        {IntroRole, "intro"},
        {KindRole, "kind"},
        {LastChapterRole, "lastChapter"},
        {BookUrlRole, "bookUrl"}
    };
}

bool LegadoBookModel::loading() const { return loading_; }
bool LegadoBookModel::detailLoading() const { return detailLoading_; }
bool LegadoBookModel::tocLoading() const { return tocLoading_; }
bool LegadoBookModel::contentLoading() const { return contentLoading_; }
QString LegadoBookModel::message() const { return message_; }
int LegadoBookModel::count() const { return items_.size(); }
int LegadoBookModel::currentPage() const { return currentPage_; }
bool LegadoBookModel::hasNextPage() const { return hasNextPage_; }
bool LegadoBookModel::hasSource() const { return hasSource_; }
bool LegadoBookModel::isAudioSource() const { return hasSource_ && currentSource_.type == 1; }
bool LegadoBookModel::isImageSource() const { return hasSource_ && currentSource_.type == 2; }
bool LegadoBookModel::hasExplore() const { return !exploreEntries_.isEmpty(); }
QVariantList LegadoBookModel::exploreEntries() const { return exploreEntries_; }
QString LegadoBookModel::sourceName() const {
    return hasSource_ ? currentSource_.name : QStringLiteral("未选择书源");
}
QVariantMap LegadoBookModel::detail() const { return detail_; }
bool LegadoBookModel::hasDetail() const { return hasDetail_; }
int LegadoBookModel::chapterCount() const { return chapters_.size(); }
QString LegadoBookModel::content() const { return content_; }
QString LegadoBookModel::contentChapterTitle() const { return contentChapterTitle_; }
int LegadoBookModel::contentChapterIndex() const { return contentChapterIndex_; }

QVariantList LegadoBookModel::chapters() const {
    QVariantList list;
    for (const ChapterItem &chapter : chapters_) {
        list.append(QVariantMap{
            {QStringLiteral("title"), chapter.title},
            {QStringLiteral("url"), chapter.url},
            {QStringLiteral("isVolume"), chapter.isVolume}
        });
    }
    return list;
}

void LegadoBookModel::setLoading(bool value) {
    if (loading_ == value)
        return;
    loading_ = value;
    emit loadingChanged();
}

void LegadoBookModel::setDetailLoading(bool value) {
    if (detailLoading_ == value)
        return;
    detailLoading_ = value;
    emit detailLoadingChanged();
}

void LegadoBookModel::setTocLoading(bool value) {
    if (tocLoading_ == value)
        return;
    tocLoading_ = value;
    emit tocLoadingChanged();
}

void LegadoBookModel::setContentLoading(bool value) {
    if (contentLoading_ == value)
        return;
    contentLoading_ = value;
    emit contentLoadingChanged();
}

void LegadoBookModel::setMessage(const QString &message) {
    if (message_ == message)
        return;
    message_ = message;
    emit messageChanged();
}

void LegadoBookModel::setSourceModel(LegadoSourceModel *sourceModel) {
    sourceModel_ = sourceModel;
}

void LegadoBookModel::applyActiveSource() {
    const BookSource source = sourceModel_ ? sourceModel_->selectedSource() : BookSource{};
    hasSource_ = !source.url.isEmpty();
    currentSource_ = source;
    ++generation_;

    beginResetModel();
    items_.clear();
    endResetModel();
    currentPage_ = 1;
    hasNextPage_ = false;
    detail_ = QVariantMap();
    hasDetail_ = false;
    chapters_.clear();
    content_ = QString();
    contentChapterTitle_ = QString();
    contentChapterIndex_ = -1;
    emit countChanged();
    emit pageChanged();
    emit detailChanged();
    emit tocChanged();
    emit contentChanged();

    if (hasSource_)
        engine_->loadJsLib(&currentSource_);
    rebuildExploreEntries();
    emit sourceChanged();
    setMessage(hasSource_
        ? QStringLiteral("当前书源: %1").arg(currentSource_.name)
        : QStringLiteral("请先导入并选择书源"));
}

void LegadoBookModel::rebuildExploreEntries() {
    exploreEntries_.clear();
    const QString raw = currentSource_.exploreUrl.trimmed();
    if (hasSource_ && !raw.isEmpty() && currentSource_.enabledExplore) {
        QString text = raw;
        if (text.startsWith(QLatin1String("@js:")) || text.contains(QLatin1String("<js>"))) {
            RuleContext ctx = makeContext(currentSource_.url, QString(), 1);
            text = engine_->evaluateString(raw, ctx, RuleNode::fromText(QString()));
        }
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            const int separator = static_cast<int>(trimmed.indexOf(QStringLiteral("::")));
            if (separator > 0) {
                exploreEntries_.append(QVariantMap{
                    {QStringLiteral("name"), trimmed.left(separator).trimmed()},
                    {QStringLiteral("url"), trimmed.mid(separator + 2).trimmed()}
                });
            } else {
                exploreEntries_.append(QVariantMap{
                    {QStringLiteral("name"), currentSource_.name},
                    {QStringLiteral("url"), trimmed}
                });
            }
        }
    }
    emit sourceChanged();
}

void LegadoBookModel::clearResults() {
    ++generation_;
    beginResetModel();
    items_.clear();
    endResetModel();
    currentPage_ = 1;
    hasNextPage_ = false;
    emit countChanged();
    emit pageChanged();
}

bool LegadoBookModel::ensureSourceReady() {
    if (!hasSource_) {
        setMessage(QStringLiteral("请先导入并选择书源"));
        return false;
    }
    if (!currentSource_.enabled) {
        setMessage(QStringLiteral("当前书源已停用"));
        return false;
    }
    return true;
}

RuleContext LegadoBookModel::makeContext(const QString &baseUrl, const QString &key,
                                         int page) {
    RuleContext ctx;
    ctx.baseUrl = baseUrl;
    ctx.key = key;
    ctx.page = page;
    ctx.source = &currentSource_;
    ctx.getVariable = [this]() -> QString {
        if (sourceModel_)
            return sourceModel_->sourceVariable(sourceModel_->selectedIndex());
        return currentSource_.variable;
    };
    ctx.setVariable = [this](const QString &value) {
        if (sourceModel_)
            sourceModel_->setSourceVariable(sourceModel_->selectedIndex(), value);
        currentSource_.variable = value;
    };
    return ctx;
}

// 同步抓取并准备上下文；返回 false 时已写入 message，调用方只需复位 loading。
bool LegadoBookModel::doFetch(const QString &urlRule, const QString &key, int page,
                              const QString &stageName, RuleContext *ctx,
                              LegadoFetchResult *result) {
    engine_->loadJsLib(&currentSource_);
    *ctx = makeContext(currentSource_.url, key, page);
    *result = client_.fetchRule(urlRule, ctx->baseUrl, key, page);
    if (!result->ok) {
        setMessage(QStringLiteral("%1失败: %2").arg(stageName, result->error));
        return false;
    }
    ctx->result = result->text;
    ctx->baseUrl = result->finalUrl.isEmpty() ? currentSource_.url : result->finalUrl;
    return true;
}

void LegadoBookModel::fillBooksFromRules(const RuleContext &ctx, const QVariantMap &rules,
                                         bool append) {
    const QString listRule = ruleField(rules, "bookList");
    if (listRule.isEmpty()) {
        setMessage(QStringLiteral("书源缺少 bookList 规则"));
        return;
    }
    const QVector<RuleNode> nodes = engine_->evaluateList(listRule, ctx);
    QVector<SearchItem> parsed;
    for (const RuleNode &node : nodes) {
        SearchItem item;
        item.name = engine_->evaluateString(ruleField(rules, "name"), ctx, node).simplified();
        item.bookUrl = absolutize(
            engine_->evaluateString(ruleField(rules, "bookUrl"), ctx, node), ctx.baseUrl);
        if (item.name.isEmpty() && item.bookUrl.isEmpty())
            continue;
        item.author = engine_->evaluateString(ruleField(rules, "author"), ctx, node).simplified();
        item.coverUrl = absolutize(
            engine_->evaluateString(ruleField(rules, "coverUrl"), ctx, node), ctx.baseUrl);
        item.intro = engine_->evaluateString(ruleField(rules, "intro"), ctx, node).simplified();
        item.kind = engine_->evaluateString(ruleField(rules, "kind"), ctx, node).simplified();
        item.lastChapter = engine_->evaluateString(ruleField(rules, "lastChapter"), ctx, node)
            .simplified();
        parsed.append(item);
    }
    if (parsed.isEmpty()) {
        setMessage(QStringLiteral("未解析到书籍列表（可能规则不支持或无结果）"));
        return;
    }
    if (!append) {
        beginResetModel();
        items_ = parsed;
        endResetModel();
    } else {
        beginInsertRows(QModelIndex(), items_.size(), items_.size() + parsed.size() - 1);
        for (const SearchItem &item : parsed)
            items_.append(item);
        endInsertRows();
    }
    emit countChanged();
    setMessage(QStringLiteral("共 %1 本").arg(items_.size()));
}

void LegadoBookModel::search(const QString &keywordText) {
    if (loading_)
        return;
    if (!ensureSourceReady())
        return;
    const QString trimmed = keywordText.trimmed();
    if (trimmed.isEmpty()) {
        setMessage(QStringLiteral("请输入搜索关键字"));
        return;
    }
    if (currentSource_.searchUrl.trimmed().isEmpty()) {
        setMessage(QStringLiteral("该书源不支持搜索"));
        return;
    }
    keyword_ = trimmed;
    clearResults();
    setLoading(true);
    const quint64 generation = generation_;

    RuleContext ctx;
    LegadoFetchResult result;
    if (!doFetch(currentSource_.searchUrl, keyword_, 1, QStringLiteral("搜索"), &ctx, &result)) {
        setLoading(false);
        return;
    }
    if (generation != generation_) {
        setLoading(false);
        return;
    }
    fillBooksFromRules(ctx, currentSource_.ruleSearch, false);
    currentPage_ = 1;
    hasNextPage_ = true;
    emit pageChanged();
    setLoading(false);
}

void LegadoBookModel::loadMore() {
    if (loading_ || !ensureSourceReady())
        return;
    const int nextPage = currentPage_ + 1;
    setLoading(true);
    const quint64 generation = generation_;

    RuleContext ctx;
    LegadoFetchResult result;
    if (!doFetch(currentSource_.searchUrl, keyword_, nextPage, QStringLiteral("搜索"),
                 &ctx, &result)) {
        setLoading(false);
        return;
    }
    if (generation != generation_) {
        setLoading(false);
        return;
    }
    fillBooksFromRules(ctx, currentSource_.ruleSearch, true);
    currentPage_ = nextPage;
    emit pageChanged();
    setLoading(false);
}

void LegadoBookModel::loadExploreEntry(int index) {
    if (loading_ || !ensureSourceReady())
        return;
    if (index < 0 || index >= exploreEntries_.size())
        return;
    const QVariantMap entry = exploreEntries_.at(index).toMap();
    const QString url = entry.value(QStringLiteral("url")).toString();
    if (url.isEmpty())
        return;
    setLoading(true);
    clearResults();
    const quint64 generation = generation_;

    RuleContext ctx;
    LegadoFetchResult result;
    if (!doFetch(url, QString(), 1, QStringLiteral("发现"), &ctx, &result)) {
        setLoading(false);
        return;
    }
    if (generation != generation_) {
        setLoading(false);
        return;
    }
    fillBooksFromRules(ctx, currentSource_.ruleExplore, false);
    setLoading(false);
}

void LegadoBookModel::loadDetail(int row) {
    if (detailLoading_ || !ensureSourceReady())
        return;
    if (row < 0 || row >= items_.size())
        return;
    const SearchItem item = items_.at(row);
    QVariantMap detail;
    detail.insert(QStringLiteral("name"), item.name);
    detail.insert(QStringLiteral("author"), item.author);
    detail.insert(QStringLiteral("cover"), item.coverUrl);
    detail.insert(QStringLiteral("intro"), item.intro);
    detail.insert(QStringLiteral("kind"), item.kind);
    detail.insert(QStringLiteral("lastChapter"), item.lastChapter);
    detail.insert(QStringLiteral("bookUrl"), item.bookUrl);
    detail.insert(QStringLiteral("tocUrl"), item.bookUrl);
    detail_ = detail;
    hasDetail_ = true;
    emit detailChanged();
    runDetail(item.bookUrl);
}

void LegadoBookModel::runDetail(const QString &bookUrl) {
    const QVariantMap rules = currentSource_.ruleBookInfo;
    if (rules.isEmpty() || bookUrl.isEmpty()) {
        emit detailChanged();
        return;
    }
    setDetailLoading(true);
    const quint64 generation = generation_;

    RuleContext ctx;
    LegadoFetchResult result;
    if (!doFetch(bookUrl, keyword_, 1, QStringLiteral("详情"), &ctx, &result)) {
        setDetailLoading(false);
        return;
    }
    if (generation != generation_) {
        setDetailLoading(false);
        return;
    }

    auto field = [&](const char *key) {
        return engine_->evaluateString(ruleField(rules, key), ctx,
            RuleNode::fromText(ctx.result));
    };
    const QString name = field("name");
    if (!name.isEmpty())
        detail_.insert(QStringLiteral("name"), name.simplified());
    const QString author = field("author");
    if (!author.isEmpty())
        detail_.insert(QStringLiteral("author"), author.simplified());
    const QString intro = field("intro");
    if (!intro.isEmpty())
        detail_.insert(QStringLiteral("intro"), intro.trimmed());
    const QString kind = field("kind");
    if (!kind.isEmpty())
        detail_.insert(QStringLiteral("kind"), kind.simplified());
    const QString cover = field("coverUrl");
    if (!cover.isEmpty())
        detail_.insert(QStringLiteral("cover"), absolutize(cover, ctx.baseUrl));
    const QString lastChapter = field("lastChapter");
    if (!lastChapter.isEmpty())
        detail_.insert(QStringLiteral("lastChapter"), lastChapter.simplified());
    QString tocUrl = field("tocUrl");
    if (tocUrl.isEmpty())
        tocUrl = detail_.value(QStringLiteral("bookUrl")).toString();
    else
        tocUrl = absolutize(tocUrl, ctx.baseUrl);
    detail_.insert(QStringLiteral("tocUrl"), tocUrl);
    hasDetail_ = true;
    emit detailChanged();
    setDetailLoading(false);
}

void LegadoBookModel::loadToc() {
    if (tocLoading_ || !ensureSourceReady())
        return;
    QString tocUrl = detail_.value(QStringLiteral("tocUrl")).toString();
    if (tocUrl.isEmpty())
        tocUrl = detail_.value(QStringLiteral("bookUrl")).toString();
    if (currentSource_.ruleToc.isEmpty() || tocUrl.isEmpty()) {
        setMessage(QStringLiteral("缺少目录规则或目录地址"));
        return;
    }
    setTocLoading(true);
    const quint64 generation = generation_;

    QStringList nextChain;
    QString url = tocUrl;
    for (int hop = 0; hop <= kMaxNextTocHops; ++hop) {
        RuleContext ctx;
        LegadoFetchResult result;
        if (!doFetch(url, keyword_, 1, QStringLiteral("目录"), &ctx, &result)) {
            setTocLoading(false);
            return;
        }
        if (generation != generation_) {
            setTocLoading(false);
            return;
        }

        const QVariantMap rules = currentSource_.ruleToc;
        const QString listRule = ruleField(rules, "chapterList");
        if (listRule.isEmpty()) {
            setTocLoading(false);
            setMessage(QStringLiteral("书源缺少 chapterList 规则"));
            return;
        }
        const QVector<RuleNode> nodes = engine_->evaluateList(listRule, ctx);
        QVector<ChapterItem> parsed;
        for (const RuleNode &node : nodes) {
            ChapterItem chapter;
            chapter.title = engine_->evaluateString(ruleField(rules, "chapterName"), ctx, node)
                .simplified();
            chapter.url = absolutize(
                engine_->evaluateString(ruleField(rules, "chapterUrl"), ctx, node), ctx.baseUrl);
            const QString volume = engine_->evaluateString(ruleField(rules, "isVolume"), ctx, node)
                .toLower();
            chapter.isVolume = volume.contains(QLatin1String("true")) || volume == QLatin1String("1");
            if (chapter.title.isEmpty() && chapter.url.isEmpty())
                continue;
            if (chapter.url.isEmpty())
                chapter.isVolume = true;
            parsed.append(chapter);
        }
        if (!parsed.isEmpty()) {
            beginInsertRows(QModelIndex(), chapters_.size(),
                chapters_.size() + parsed.size() - 1);
            for (const ChapterItem &chapter : parsed)
                chapters_.append(chapter);
            endInsertRows();
            emit tocChanged();
        }

        const QString nextRule = ruleField(rules, "nextTocUrl");
        QString nextUrl;
        if (!nextRule.isEmpty() && hop < kMaxNextTocHops) {
            nextUrl = absolutize(
                engine_->evaluateString(nextRule, ctx, RuleNode::fromText(ctx.result)),
                ctx.baseUrl);
        }
        if (nextUrl.isEmpty() || nextUrl == url)
            break;
        url = nextUrl;
    }

    setTocLoading(false);
    if (chapters_.isEmpty())
        setMessage(QStringLiteral("未解析到章节（可能规则不支持）"));
    else
        setMessage(QStringLiteral("共 %1 章").arg(chapters_.size()));
}

void LegadoBookModel::loadContent(int chapterIndex) {
    runContent(chapterIndex, false);
}

void LegadoBookModel::playChapter(int chapterIndex) {
    runContent(chapterIndex, true);
}

void LegadoBookModel::runContent(int chapterIndex, bool forPlayback) {
    if (contentLoading_ || !ensureSourceReady())
        return;
    if (chapterIndex < 0 || chapterIndex >= chapters_.size()) {
        setMessage(QStringLiteral("章节不存在"));
        return;
    }
    const ChapterItem chapter = chapters_.at(chapterIndex);
    const QVariantMap rules = currentSource_.ruleContent;
    const QString contentRule = ruleField(rules, "content");
    if (contentRule.isEmpty() || chapter.url.isEmpty()) {
        setMessage(QStringLiteral("缺少正文规则或章节地址"));
        return;
    }
    setContentLoading(true);
    const quint64 generation = generation_;

    QStringList collected;
    QString url = chapter.url;
    for (int hop = 0; hop <= kMaxNextContentHops; ++hop) {
        RuleContext ctx;
        LegadoFetchResult result;
        if (!doFetch(url, keyword_, 1, QStringLiteral("正文"), &ctx, &result)) {
            setContentLoading(false);
            return;
        }
        if (generation != generation_) {
            setContentLoading(false);
            return;
        }
        ctx.title = chapter.title;

        const QStringList lines = engine_->evaluateStrings(contentRule, ctx,
            RuleNode::fromText(ctx.result));
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty())
                collected.append(line.trimmed());
        }

        const QString nextRule = ruleField(rules, "nextContentUrl");
        QString nextUrl;
        if (!nextRule.isEmpty() && hop < kMaxNextContentHops) {
            nextUrl = absolutize(
                engine_->evaluateString(nextRule, ctx, RuleNode::fromText(ctx.result)),
                ctx.baseUrl);
        }
        if (nextUrl.isEmpty() || nextUrl == url)
            break;
        url = nextUrl;
    }

    const QString content = collected.join(QStringLiteral("\n"));
    setContentLoading(false);
    if (content.isEmpty()) {
        setMessage(QStringLiteral("未解析到正文（可能规则不支持）"));
        return;
    }

    if (forPlayback || currentSource_.type == 1) {
        // 音频源：正文规则输出媒体直链
        const QStringList candidates = content.split(
            QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        QString mediaUrl;
        for (const QString &candidate : candidates) {
            const QString trimmed = candidate.trimmed();
            if (trimmed.contains(QLatin1String("://"))) {
                mediaUrl = trimmed;
                break;
            }
        }
        if (mediaUrl.isEmpty() && !content.trimmed().contains(QLatin1Char(' ')))
            mediaUrl = content.trimmed();
        if (mediaUrl.isEmpty()) {
            setMessage(QStringLiteral("未解析到音频地址"));
            return;
        }
        emit playbackRequested(mediaUrl, chapter.title);
        return;
    }

    content_ = content;
    contentChapterTitle_ = chapter.title;
    contentChapterIndex_ = chapterIndex;
    emit contentChanged();
}

QString LegadoBookModel::absolutize(const QString &value, const QString &baseUrl) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    if (trimmed.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return trimmed;
    const QUrl resolved = QUrl(baseUrl).resolved(QUrl(trimmed));
    return resolved.toString();
}
