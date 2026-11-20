#include "app/AppServices.h"

#include "models/VideoSearchModel.h"

AppServices::AppServices(QObject *parent)
    : QObject(parent),
      authController_(new AuthController(this)),
      videoCatalogController_(new VideoCatalogController(&apiSiteModel_, &downloadModel_, this)),
      imageController_(new ImageController(&apiSiteModel_, this)),
      memeController_(new MemeController(this)),
      voiceController_(new VoiceController(this)),
      updateController_(new UpdateController(this)),
      poolMeController_(new PoolMeController(authController_, this)),
      poolChatController_(new PoolChatController(authController_, this)) {
    // pool 统一点播源凭据同步：登录态/服务器变化时刷新站点表与会话令牌。
    // 恢复的会话在构造时即可用，所以这里也同步一次。
    const auto syncPoolCredentials = [this]() {
        apiSiteModel_.setPoolCredentials(authController_->serverUrl(),
                                         authController_->sessionToken());
        VideoSearchModel::setPoolSessionToken(authController_->sessionToken());
        videoCatalogController_->loadWeeklySchedule();
    };
    syncPoolCredentials();
    connect(authController_, &AuthController::loginSuccess, this,
            syncPoolCredentials);
    // 持久化 token 自动登录（tryAutoLogin 恢复分支）只发 loginStatusChanged，
    // 且它在 AppServices 构造完成后的下一轮事件循环才运行——必须监听它，
    // 否则重启后 apiSiteModel 拿不到 pool 凭据，站点下拉退回本地旧清单。
    connect(authController_, &AuthController::loginStatusChanged, this,
            syncPoolCredentials);
    connect(authController_, &AuthController::loggedOut, this,
            syncPoolCredentials);
    connect(authController_, &AuthController::serverUrlChanged, this,
            syncPoolCredentials);
}
