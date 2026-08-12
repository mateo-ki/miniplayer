#include "media/HlsPlaylistFilter.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <limits>

namespace {
struct SegmentInfo {
    int uriLine = -1;
    int startLine = -1;
    QString key;
    qint64 number = -1;
    bool discontinuityBefore = false;
};

QString absoluteUrl(const QString &value, const QUrl &playlistUrl) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
        return trimmed;
    return playlistUrl.resolved(QUrl(trimmed)).toString();
}

QString rewriteTagUris(const QString &line, const QUrl &playlistUrl) {
    static const QRegularExpression uriAttribute(QStringLiteral("URI=\\\"([^\\\"]+)\\\""));
    QString result = line;
    int offset = 0;
    while (true) {
        const auto match = uriAttribute.match(result, offset);
        if (!match.hasMatch())
            break;
        const QString resolved = absoluteUrl(match.captured(1), playlistUrl);
        const int valueStart = match.capturedStart(1);
        result.replace(valueStart, match.capturedLength(1), resolved);
        offset = valueStart + resolved.size();
    }
    return result;
}

bool parseSegment(const QString &line, QString *key, qint64 *number) {
    QString path = QUrl(line.trimmed()).path();
    if (path.isEmpty())
        path = line.trimmed().section('?', 0, 0).section('#', 0, 0);
    const QString fileName = path.section('/', -1);
    static const QRegularExpression numberedName(QStringLiteral("^(.*?)([0-9]{2,})(\\.[^./]+)$"));
    const auto match = numberedName.match(fileName);
    if (!match.hasMatch())
        return false;
    bool ok = false;
    const qint64 parsedNumber = match.captured(2).toLongLong(&ok);
    if (!ok)
        return false;
    *key = match.captured(1) + match.captured(3);
    *number = parsedNumber;
    return true;
}
}

HlsPlaylistFilterResult HlsPlaylistFilter::filterOutOfSequenceAds(const QString &playlist,
                                                                   const QUrl &playlistUrl) {
    QStringList lines = playlist.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    QVector<SegmentInfo> segments;
    bool hasExtInf = false;
    bool discontinuitySinceLastSegment = false;
    int pendingStart = -1;

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith(QStringLiteral("#EXT-X-DISCONTINUITY"))) {
            discontinuitySinceLastSegment = true;
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("#EXTINF:"))) {
            hasExtInf = true;
            pendingStart = i;
            continue;
        }
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;

        QString key;
        qint64 number = -1;
        if (pendingStart >= 0 && parseSegment(trimmed, &key, &number)) {
            segments.append({i, pendingStart, key, number, discontinuitySinceLastSegment});
        }
        pendingStart = -1;
    }

    if (!hasExtInf || segments.isEmpty())
        return {playlist, 0};

    QHash<QString, QVector<int>> groups;
    for (int i = 0; i < segments.size(); ++i)
        groups[segments[i].key].append(i);

    QSet<int> skippedLines;
    int skippedSegments = 0;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const auto &indices = it.value();
        if (indices.size() < 3)
            continue;

        qint64 minimum = std::numeric_limits<qint64>::max();
        qint64 maximum = 0;
        for (const int index : indices) {
            minimum = qMin(minimum, segments[index].number);
            maximum = qMax(maximum, segments[index].number);
        }
        if (maximum < 1000 || maximum - minimum < 10000)
            continue;

        const qint64 lowCutoff = maximum / 100;
        for (const int index : indices) {
            const auto &segment = segments[index];
            if (segment.number > lowCutoff || !segment.discontinuityBefore)
                continue;
            for (int line = segment.startLine; line <= segment.uriLine; ++line)
                skippedLines.insert(line);
            ++skippedSegments;
        }
    }

    if (skippedSegments == 0)
        return {playlist, 0};

    QStringList filtered;
    filtered.reserve(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        if (skippedLines.contains(i))
            continue;
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith(QLatin1Char('#')))
            filtered.append(rewriteTagUris(trimmed, playlistUrl));
        else if (!trimmed.isEmpty())
            filtered.append(absoluteUrl(trimmed, playlistUrl));
        else
            filtered.append(QString());
    }
    return {filtered.join(QLatin1Char('\n')), skippedSegments};
}
