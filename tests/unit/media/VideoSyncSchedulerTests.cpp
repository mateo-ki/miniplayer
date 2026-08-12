#include <QtTest>

#include "media/VideoSyncScheduler.h"

class VideoSyncSchedulerTests : public QObject {
    Q_OBJECT

private slots:
    void dropsLateFrame();
    void waitsForEarlyFrame();
    void presentsOnTimeFrame();
    void computeDelayReturnsZeroForPastFrame();
    void computeDelayReturnsDifferenceForFutureFrame();
    void zeroPtsFrame();
    void negativeClockHandled();
    void largeTolerancePresentsAll();
};

void VideoSyncSchedulerTests::dropsLateFrame() {
    VideoSyncScheduler scheduler;
    QCOMPARE(scheduler.evaluate(1.0, 1.1, 0.05), VideoSyncScheduler::Decision::Drop);
}

void VideoSyncSchedulerTests::waitsForEarlyFrame() {
    VideoSyncScheduler scheduler;
    QCOMPARE(scheduler.evaluate(1.1, 1.0, 0.05), VideoSyncScheduler::Decision::Wait);
}

void VideoSyncSchedulerTests::presentsOnTimeFrame() {
    VideoSyncScheduler scheduler;
    QCOMPARE(scheduler.evaluate(1.0, 1.0, 0.05), VideoSyncScheduler::Decision::Present);
    QCOMPARE(scheduler.evaluate(0.98, 1.0, 0.05), VideoSyncScheduler::Decision::Present);
}

void VideoSyncSchedulerTests::computeDelayReturnsZeroForPastFrame() {
    VideoSyncScheduler scheduler;
    QCOMPARE(scheduler.computeDelay(0.5, 1.0), 0.0);
    QCOMPARE(scheduler.computeDelay(1.0, 1.0), 0.0);
}

void VideoSyncSchedulerTests::computeDelayReturnsDifferenceForFutureFrame() {
    VideoSyncScheduler scheduler;
    QCOMPARE(scheduler.computeDelay(1.5, 1.0), 0.5);
}

void VideoSyncSchedulerTests::zeroPtsFrame() {
    VideoSyncScheduler scheduler;
    // First frame at PTS=0, clock=0
    QCOMPARE(scheduler.evaluate(0.0, 0.0, 0.05), VideoSyncScheduler::Decision::Present);
}

void VideoSyncSchedulerTests::negativeClockHandled() {
    VideoSyncScheduler scheduler;
    // Clock slightly negative, PTS=0 → frame is "early" (PTS > clock) → Wait
    QCOMPARE(scheduler.evaluate(0.0, -0.01, 0.05), VideoSyncScheduler::Decision::Wait);
}

void VideoSyncSchedulerTests::largeTolerancePresentsAll() {
    VideoSyncScheduler scheduler;
    // With very large tolerance, even "late" frames should present
    QCOMPARE(scheduler.evaluate(1.0, 2.0, 5.0), VideoSyncScheduler::Decision::Present);
}

int main(int argc, char **argv) {
    VideoSyncSchedulerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "VideoSyncSchedulerTests.moc"
