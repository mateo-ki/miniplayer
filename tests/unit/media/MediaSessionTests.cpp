#include <QtTest>

#include "core/MediaSession.h"
#include "media/HlsPlaylistFilter.h"

class MediaSessionTests : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingFile();
    void removesLowNumberedDiscontinuityAdSegments();
};

void MediaSessionTests::rejectsMissingFile() {
    MediaSession session;

    const Error result = session.open("Z:/missing-file.mp4");

    QCOMPARE(result.ok, false);
    QVERIFY(result.message.contains("exist"));
}

void MediaSessionTests::removesLowNumberedDiscontinuityAdSegments() {
    const QString playlist = QStringLiteral(
        "#EXTM3U\n"
        "#EXTINF:4.000,\n"
        "ce888d66abc0645293.ts\n"
        "#EXTINF:4.000,\n"
        "ce888d66abc0645294.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:5.000,\n"
        "ce888d66abc000070.ts\n"
        "#EXTINF:4.000,\n"
        "ce888d66abc000071.ts\n"
        "#EXTINF:2.800,\n"
        "ce888d66abc000072.ts\n"
        "#EXTINF:4.000,\n"
        "ce888d66abc000073.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:4.000,\n"
        "ce888d66abc0645295.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:5.000,\n"
        "ce888d66abc000074.ts\n"
        "#EXTINF:2.960,\n"
        "ce888d66abc000075.ts\n"
        "#EXTINF:4.080,\n"
        "ce888d66abc000076.ts\n"
        "#EXT-X-ENDLIST\n");

    const auto result = HlsPlaylistFilter::filterOutOfSequenceAds(
        playlist, QUrl(QStringLiteral("https://v.lzcdn31.com/20260808/9516_8c3f3609/2000k/hls/mixed.m3u8")));

    QCOMPARE(result.skippedSegments, 7);
    QVERIFY(!result.playlist.contains(QStringLiteral("ce888d66abc000070.ts")));
    QVERIFY(!result.playlist.contains(QStringLiteral("ce888d66abc000071.ts")));
    QVERIFY(!result.playlist.contains(QStringLiteral("ce888d66abc000076.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("https://v.lzcdn31.com/20260808/9516_8c3f3609/2000k/hls/ce888d66abc0645293.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("https://v.lzcdn31.com/20260808/9516_8c3f3609/2000k/hls/ce888d66abc0645295.ts")));
}

int main(int argc, char **argv) {
    MediaSessionTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "MediaSessionTests.moc"
