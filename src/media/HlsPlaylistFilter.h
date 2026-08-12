#pragma once

#include <QString>
#include <QUrl>

struct HlsPlaylistFilterResult {
    QString playlist;
    int skippedSegments = 0;
};

class HlsPlaylistFilter final {
public:
    static HlsPlaylistFilterResult filterOutOfSequenceAds(const QString &playlist,
                                                           const QUrl &playlistUrl);
};
