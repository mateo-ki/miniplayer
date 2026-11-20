#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>

#include "DmghgClientV3.h"

/**
 * 新旧算法切换开关。
 *
 * 优先级: 环境变量 DMGHG_PROTOCOL > QSettings(dmghg/protocol) > 默认 V3。
 * 取值 "old"/"legacy" 走旧的 AES-256-CBC 时间桶 IV + RSA 响应，
 * 其它（含 "new"/"v3"）走新协议。旧链路完整保留，新协议一旦不可用可立即回退。
 */
namespace dmghg {

enum class Protocol {
    Legacy,
    V3,
};

inline Protocol configuredProtocol() {
    QByteArray raw = qgetenv("DMGHG_PROTOCOL").trimmed();
    if (raw.isEmpty()) {
        QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
        raw = s.value(QStringLiteral("dmghg/protocol")).toString().trimmed().toLatin1();
    }
    const QString v = QString::fromLatin1(raw).toLower();
    if (v == QLatin1String("old") || v == QLatin1String("legacy") || v == QLatin1String("v1"))
        return Protocol::Legacy;
    return Protocol::V3;
}

inline bool useV3() { return configuredProtocol() == Protocol::V3; }

inline void setUseV3(bool v3) {
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    s.setValue(QStringLiteral("dmghg/protocol"), v3 ? QStringLiteral("new") : QStringLiteral("old"));
}

}  // namespace dmghg
