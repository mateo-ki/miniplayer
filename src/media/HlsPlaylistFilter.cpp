#include "media/HlsPlaylistFilter.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace {
struct SegmentSequence {
    QString prefix;
    qint64 number = -1;
};

bool parseSegmentSequence(const QString &value, SegmentSequence *sequence) {
    QString path = QUrl(value.trimmed()).path();
    if (path.isEmpty())
        path = value.trimmed().section('?', 0, 0).section('#', 0, 0);
    const QString fileName = path.section('/', -1);
    static const QRegularExpression numberedName(QStringLiteral("^(.*?)([0-9]+)(\\.[^./]+)$"));
    const auto match = numberedName.match(fileName);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    const qint64 number = match.captured(2).toLongLong(&ok);
    if (!ok)
        return false;

    sequence->prefix = match.captured(1);
    sequence->number = number;
    return true;
}

QVector<SegmentSequence> segmentSequences(const QStringList &lines) {
    QVector<SegmentSequence> sequences;
    bool expectsSegment = false;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("#EXTINF:"))) {
            expectsSegment = true;
            continue;
        }
        if (!expectsSegment || trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;

        expectsSegment = false;
        SegmentSequence sequence;
        if (parseSegmentSequence(trimmed, &sequence))
            sequences.append(sequence);
    }
    return sequences;
}

int segmentCount(const QStringList &lines) {
    int count = 0;
    bool expectsSegment = false;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("#EXTINF:"))) {
            expectsSegment = true;
            continue;
        }
        if (!expectsSegment || trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        expectsSegment = false;
        ++count;
    }
    return count;
}

bool isLargeSequenceJump(qint64 previous, qint64 current) {
    const qint64 minimumJump = qBound<qint64>(qint64(100), previous / 10, qint64(10000));
    return qAbs(previous - current) >= minimumJump;
}

bool continuesSequence(const SegmentSequence &previous, const SegmentSequence &current) {
    if (previous.prefix != current.prefix || current.number <= previous.number)
        return false;
    return current.number - previous.number <= 100;
}
}

HlsPlaylistFilterResult HlsPlaylistFilter::filterOutOfSequenceAds(const QString &playlist,
                                                                   const QUrl &) {
    if (!playlist.contains(QStringLiteral("#EXT-X-DISCONTINUITY")))
        return {playlist, 0};

    const QStringList lines = playlist.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    QVector<QStringList> sections(1);
    for (const QString &line : lines) {
        if (line.trimmed() == QStringLiteral("#EXT-X-DISCONTINUITY"))
            sections.append(QStringList());
        else
            sections.last().append(line);
    }

    QVector<QStringList> accepted;
    SegmentSequence lastAccepted;
    bool hasLastAccepted = false;
    int skippedSegments = 0;

    QVector<QVector<SegmentSequence>> sectionSequences;
    sectionSequences.reserve(sections.size());
    for (const QStringList &section : sections)
        sectionSequences.append(segmentSequences(section));

    for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
        const QStringList &section = sections[sectionIndex];
        const auto &sequences = sectionSequences[sectionIndex];
        const bool outOfSequence = !sequences.isEmpty()
            && hasLastAccepted
            && sequences.first().prefix == lastAccepted.prefix
            && isLargeSequenceJump(lastAccepted.number, sequences.first().number);

        bool interruptsRecoverableSequence = false;
        if (!sequences.isEmpty() && hasLastAccepted
            && !continuesSequence(lastAccepted, sequences.first())) {
            for (int nextIndex = sectionIndex + 1; nextIndex < sections.size(); ++nextIndex) {
                const auto &nextSequences = sectionSequences[nextIndex];
                if (nextSequences.isEmpty())
                    continue;
                interruptsRecoverableSequence = continuesSequence(lastAccepted,
                                                                    nextSequences.first());
                break;
            }
        }

        if (outOfSequence || interruptsRecoverableSequence) {
            skippedSegments += segmentCount(section);
            continue;
        }

        accepted.append(section);
        if (!sequences.isEmpty()) {
            lastAccepted = sequences.last();
            hasLastAccepted = true;
        }
    }

    if (skippedSegments == 0)
        return {playlist, 0};

    QStringList filtered;
    for (int index = 0; index < accepted.size(); ++index) {
        if (index > 0)
            filtered.append(QStringLiteral("#EXT-X-DISCONTINUITY"));
        filtered.append(accepted[index]);
    }
    if (lines.contains(QStringLiteral("#EXT-X-ENDLIST"))
        && !filtered.contains(QStringLiteral("#EXT-X-ENDLIST"))) {
        filtered.append(QStringLiteral("#EXT-X-ENDLIST"));
    }
    return {filtered.join(QLatin1Char('\n')), skippedSegments};
}
