#include "app/AppServices.h"

AppServices::AppServices(QObject *parent)
    : QObject(parent) {
    wireInternalLinks();
}

// 模型之间的内部依赖接线（与播放器无关的部分）。
void AppServices::wireInternalLinks() {
    legadoBookModel_.setSourceModel(&legadoSourceModel_);
}
