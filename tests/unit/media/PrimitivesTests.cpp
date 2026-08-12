#include <QtTest>
#include <QThread>
#include <atomic>

#include "media/AudioClock.h"
#include "media/PacketQueue.h"
#include "media/FrameQueue.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

class PacketQueueTests : public QObject {
    Q_OBJECT

private slots:
    void pushPopPreservesOrder();
    void clearEmptiesQueue();
    void abortUnblocksWaitingPop();
    void boundedPushBlocksWhenFull();
    void signalEofReturnsNull();
};

class FrameQueueTests : public QObject {
    Q_OBJECT

private slots:
    void pushPopPreservesFrame();
    void abortUnblocksWaitingPop();
    void tryPopReturnsNulloptWhenEmpty();
    void tryPopReturnsFrameWhenAvailable();
    void boundedPushBlocksWhenFull();
};

class AudioClockTests : public QObject {
    Q_OBJECT

private slots:
    void updateReadRoundTrip();
    void resetZeroes();
    void concurrentUpdates();
};

void PacketQueueTests::pushPopPreservesOrder() {
    PacketQueue queue("test");

    AVPacket *pkt1 = av_packet_alloc();
    pkt1->stream_index = 1;
    AVPacket *pkt2 = av_packet_alloc();
    pkt2->stream_index = 2;

    queue.push(pkt1);
    queue.push(pkt2);

    AVPacket *out1 = queue.pop();
    AVPacket *out2 = queue.pop();

    QCOMPARE(out1->stream_index, 1);
    QCOMPARE(out2->stream_index, 2);

    av_packet_free(&out1);
    av_packet_free(&out2);
    av_packet_free(&pkt1);
    av_packet_free(&pkt2);
}

void PacketQueueTests::clearEmptiesQueue() {
    PacketQueue queue("test");

    AVPacket *pkt = av_packet_alloc();
    pkt->stream_index = 0;
    queue.push(pkt);

    queue.clear();

    QVERIFY(queue.isEmpty());
    av_packet_free(&pkt);
}

void PacketQueueTests::abortUnblocksWaitingPop() {
    PacketQueue queue("test");
    queue.abort();

    AVPacket *result = queue.pop();
    QVERIFY(result == nullptr);

    queue.resume();
}

void PacketQueueTests::boundedPushBlocksWhenFull() {
    PacketQueue queue("test", 2);

    AVPacket *pkt1 = av_packet_alloc();
    pkt1->stream_index = 1;
    AVPacket *pkt2 = av_packet_alloc();
    pkt2->stream_index = 2;
    AVPacket *pkt3 = av_packet_alloc();
    pkt3->stream_index = 3;

    // Push 2 packets (fills the queue)
    queue.push(pkt1);
    queue.push(pkt2);

    // Third push should block; verify by popping in another thread
    std::atomic<bool> pushDone{false};
    QThread *t = QThread::create([&]() {
        queue.push(pkt3);
        pushDone = true;
    });
    t->start();

    QThread::msleep(50);
    QVERIFY(!pushDone.load()); // should still be blocked

    // Pop one to unblock
    AVPacket *out = queue.pop();
    av_packet_free(&out);

    QThread::msleep(50);
    QVERIFY(pushDone.load()); // push should have completed

    // Drain
    out = queue.pop();
    av_packet_free(&out);
    out = queue.pop();
    av_packet_free(&out);

    t->wait();
    delete t;
    av_packet_free(&pkt1);
    av_packet_free(&pkt2);
    av_packet_free(&pkt3);
}

void PacketQueueTests::signalEofReturnsNull() {
    PacketQueue queue("test");
    queue.signalEof();

    AVPacket *result = queue.pop();
    QVERIFY(result == nullptr);
}

void FrameQueueTests::pushPopPreservesFrame() {
    FrameQueue queue;

    TimedVideoFrame frame;
    frame.image = QImage(320, 240, QImage::Format_RGB32);
    frame.ptsSec = 1.5;

    queue.push(std::move(frame));

    TimedVideoFrame out = queue.pop();

    QCOMPARE(out.ptsSec, 1.5);
    QCOMPARE(out.image.width(), 320);
}

void FrameQueueTests::abortUnblocksWaitingPop() {
    FrameQueue queue;
    queue.abort();

    TimedVideoFrame result = queue.pop();
    QVERIFY(result.image.isNull());

    queue.resume();
}

void FrameQueueTests::tryPopReturnsNulloptWhenEmpty() {
    FrameQueue queue;
    auto result = queue.tryPop();
    QVERIFY(!result.has_value());
}

void FrameQueueTests::tryPopReturnsFrameWhenAvailable() {
    FrameQueue queue;

    TimedVideoFrame frame;
    frame.image = QImage(640, 480, QImage::Format_RGB32);
    frame.ptsSec = 2.5;

    queue.push(std::move(frame));

    auto result = queue.tryPop();
    QVERIFY(result.has_value());
    QCOMPARE(result->ptsSec, 2.5);
    QCOMPARE(result->image.width(), 640);
}

void FrameQueueTests::boundedPushBlocksWhenFull() {
    FrameQueue queue(2);

    TimedVideoFrame f1;
    f1.image = QImage(320, 240, QImage::Format_RGB32);
    f1.ptsSec = 1.0;
    TimedVideoFrame f2;
    f2.image = QImage(320, 240, QImage::Format_RGB32);
    f2.ptsSec = 2.0;
    TimedVideoFrame f3;
    f3.image = QImage(320, 240, QImage::Format_RGB32);
    f3.ptsSec = 3.0;

    queue.push(std::move(f1));
    queue.push(std::move(f2));

    std::atomic<bool> pushDone{false};
    QThread *t = QThread::create([&]() {
        queue.push(std::move(f3));
        pushDone = true;
    });
    t->start();

    QThread::msleep(50);
    QVERIFY(!pushDone.load());

    auto out = queue.pop();
    Q_UNUSED(out)

    QThread::msleep(50);
    QVERIFY(pushDone.load());

    // Drain
    queue.pop();
    queue.pop();

    t->wait();
    delete t;
}

void AudioClockTests::updateReadRoundTrip() {
    AudioClock clock;

    clock.update(3.14);

    QCOMPARE(clock.positionSec(), 3.14);
}

void AudioClockTests::resetZeroes() {
    AudioClock clock;
    clock.update(5.0);

    clock.reset();

    QCOMPARE(clock.positionSec(), 0.0);
}

void AudioClockTests::concurrentUpdates() {
    AudioClock clock;
    std::atomic<bool> done{false};
    std::atomic<int> readCount{0};

    // Writer thread updates at high frequency
    QThread *writer = QThread::create([&]() {
        for (int i = 0; i < 10000; ++i) {
            clock.update(static_cast<double>(i) * 0.001);
        }
        done = true;
    });
    writer->start();

    // Reader thread reads concurrently
    double lastRead = -1.0;
    while (!done.load()) {
        double val = clock.positionSec();
        QVERIFY(val >= lastRead);
        lastRead = val;
        ++readCount;
    }

    // Final read should reflect last written value
    double finalVal = clock.positionSec();
    QVERIFY(finalVal >= 0.0);
    QVERIFY(readCount.load() > 0);

    writer->wait();
    delete writer;
}

int main(int argc, char **argv) {
    int status = 0;

    {
        PacketQueueTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        FrameQueueTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        AudioClockTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }

    return status;
}

#include "PrimitivesTests.moc"
