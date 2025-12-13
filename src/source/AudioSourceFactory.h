#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ImplAudioSource.h"
#include "network/Network.h"
//封装创建音频源的逻辑，如从本地文件、内存、URL
//后续可自动识别类型（.mp3 / .flac / http buffer）
//工厂的真正职责：选择、创建、统一入口
//返回 unique_ptr<ImplAudioSource>
class AudioSourceFactory {
public:
    // 从本地文件创建音频源
    static std::unique_ptr<ImplAudioSource> fromFile(const std::string& path) {
        return std::make_unique<LocalFileSource>(path);
    }

    // 从内存 buffer 创建音频源
    static std::unique_ptr<ImplAudioSource> fromMemory(std::shared_ptr<NetworkDownloader> downloader) {
        return std::make_unique<NetworkStreamSource>(downloader);
    }

    // 根据类型自动选择构建方式
    static std::unique_ptr<ImplAudioSource> createSource(const std::string& sourcePathOrId,
        AudioSourceType type,
        const std::vector<uint8_t>* buffer = nullptr) {
        switch (type) {
        case AudioSourceType::LocalFile:
            return fromFile(sourcePathOrId);
        // 可以扩充更多规则..
        default:
            throw std::runtime_error("Unsupported AudioSourceType");
        }
    }
};
