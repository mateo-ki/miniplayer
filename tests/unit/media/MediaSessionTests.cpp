#include <QtTest>

#include "core/MediaSession.h"

class MediaSessionTests : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingFile();
};

void MediaSessionTests::rejectsMissingFile() {
    MediaSession session;

    const Error result = session.open("Z:/missing-file.mp4");

    QCOMPARE(result.ok, false);
    QVERIFY(result.message.contains("exist"));
}

int main(int argc, char **argv) {
    MediaSessionTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "MediaSessionTests.moc"
