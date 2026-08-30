#include <QtTest>

#include "core/MediaSession.h"
#include "media/HlsPlaylistFilter.h"

class MediaSessionTests : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingFile();
    void removesLowNumberedDiscontinuityAdSegments();
    void removesHighNumberedDiscontinuityAdBlock();
    void removesForeignSequenceAdBlock();
    void removesRealLz20240918AdBlock();
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
    QVERIFY(result.playlist.contains(QStringLiteral("ce888d66abc0645293.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("ce888d66abc0645295.ts")));
}

void MediaSessionTests::removesHighNumberedDiscontinuityAdBlock() {
    const QString playlist = QStringLiteral(
        "#EXTM3U\n"
        "#EXTINF:5.840,\n"
        "2b5bb909ca4000073.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:4.000,\n"
        "2b5bb909ca40551360.ts\n"
        "#EXTINF:4.000,\n"
        "2b5bb909ca40551361.ts\n"
        "#EXTINF:4.000,\n"
        "2b5bb909ca40551362.ts\n"
        "#EXTINF:4.000,\n"
        "2b5bb909ca40551363.ts\n"
        "#EXTINF:4.000,\n"
        "2b5bb909ca40551364.ts\n"
        "#EXTINF:4.000,\n"
        "2b5bb909ca40551365.ts\n"
        "#EXTINF:2.000,\n"
        "2b5bb909ca40551366.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:4.480,\n"
        "2b5bb909ca4000074.ts\n"
        "#EXTINF:4.160,\n"
        "2b5bb909ca4000075.ts\n"
        "#EXT-X-ENDLIST\n");

    const auto result = HlsPlaylistFilter::filterOutOfSequenceAds(
        playlist, QUrl(QStringLiteral("https://example.com/video/index.m3u8")));

    QCOMPARE(result.skippedSegments, 7);
    QVERIFY(!result.playlist.contains(QStringLiteral("2b5bb909ca40551360.ts")));
    QVERIFY(!result.playlist.contains(QStringLiteral("2b5bb909ca40551366.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("2b5bb909ca4000073.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("2b5bb909ca4000074.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("2b5bb909ca4000075.ts")));
}

void MediaSessionTests::removesForeignSequenceAdBlock() {
    const QString playlist = QStringLiteral(
        "#EXTM3U\n"
        "#EXTINF:4.000,\n"
        "content000073.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:5.000,\n"
        "commercial000001.ts\n"
        "#EXTINF:5.000,\n"
        "commercial000002.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:4.000,\n"
        "content000074.ts\n"
        "#EXTINF:4.000,\n"
        "content000075.ts\n"
        "#EXT-X-ENDLIST\n");

    const auto result = HlsPlaylistFilter::filterOutOfSequenceAds(
        playlist, QUrl(QStringLiteral("https://example.com/video/mixed.m3u8")));

    QCOMPARE(result.skippedSegments, 2);
    QVERIFY(!result.playlist.contains(QStringLiteral("commercial000001.ts")));
    QVERIFY(!result.playlist.contains(QStringLiteral("commercial000002.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("content000073.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("content000074.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("content000075.ts")));
}

void MediaSessionTests::removesRealLz20240918AdBlock() {
    const QString playlist = QStringLiteral(
        "#EXTM3U\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:1.560,\n"
        "3478ba90493000073.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:4.000,\n"
        "3478ba904930571305.ts\n"
        "#EXTINF:4.000,\n"
        "3478ba904930571306.ts\n"
        "#EXTINF:4.000,\n"
        "3478ba904930571307.ts\n"
        "#EXTINF:4.000,\n"
        "3478ba904930571308.ts\n"
        "#EXTINF:4.000,\n"
        "3478ba904930571309.ts\n"
        "#EXTINF:4.000,\n"
        "3478ba904930571310.ts\n"
        "#EXTINF:2.000,\n"
        "3478ba904930571311.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:2.600,\n"
        "3478ba90493000074.ts\n"
        "#EXTINF:4.880,\n"
        "3478ba90493000075.ts\n"
        "#EXT-X-ENDLIST\n");

    const auto result = HlsPlaylistFilter::filterOutOfSequenceAds(
        playlist,
        QUrl(QStringLiteral("https://v.cdnlz22.com/20240918/5229_1820b33d/2000k/hls/mixed.m3u8")));

    QCOMPARE(result.skippedSegments, 7);
    QVERIFY(!result.playlist.contains(QStringLiteral("3478ba904930571305.ts")));
    QVERIFY(!result.playlist.contains(QStringLiteral("3478ba904930571311.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("3478ba90493000073.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("3478ba90493000074.ts")));
    QVERIFY(result.playlist.contains(QStringLiteral("3478ba90493000075.ts")));
}

int main(int argc, char **argv) {
    MediaSessionTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "MediaSessionTests.moc"
