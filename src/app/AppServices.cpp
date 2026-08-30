#include "app/AppServices.h"

AppServices::AppServices(QObject *parent)
    : QObject(parent),
      videoCatalogController_(new VideoCatalogController(&apiSiteModel_, &downloadModel_, this)),
      imageController_(new ImageController(&apiSiteModel_, this)),
      memeController_(new MemeController(this)),
      voiceController_(new VoiceController(this)),
      hotNewsController_(new HotNewsController(this)),
      updateController_(new UpdateController(this)) {
    wireInternalLinks();
}

// 模型之间的内部依赖接线（与播放器无关的部分）。
void AppServices::wireInternalLinks() {
    legadoBookModel_.setSourceModel(&legadoSourceModel_);
}
