#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h" 
#include "source/AudioSourceFactory.h"
#include "player/AudioPlayer.h"
#include "network/Network.h"
#include "utils/Logger.h"

int main()
{
    // 创建播放器
    AudioPlayer player;

    // Test URL
    std::string url = "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-5.mp3";

    // 单例模式 下载管理器
    auto& Mgr = NetworkDownloadMgr::getInstance();
    
    // 获取一个传输实例，有接口实现下载到本地
    auto downloader = Mgr.getDownloader(url);

    // 尝试建立连接
    downloader->start();

    // 尝试用工厂模式，把音频源视作对象 but... todooooo..
    auto netsrc= AudioSourceFactory::fromMemory(downloader);

    player.setSource(std::move(netsrc));
    player.play();
    LOG_INFO(" Prepare for playing...May wait for seconds...");
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    while (true) {
        std::cout << "to wait for buffer" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    getchar();
    return 0;
}
