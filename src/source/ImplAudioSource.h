#pragma once
#include "AudioSourceType.h"
#include "miniaudio.h"
#include "network/Network.h"

// 统一的接口 运行时多态
class ImplAudioSource {
public:
    virtual ~ImplAudioSource() = default;
    virtual ma_uint64 read(void* pOutput, const void* pInput, ma_uint32 frameCount) = 0;
    virtual ma_result seek(float percent) = 0;      // 返回值表示请求是否成功
    virtual AudioSourceType SourceType() const = 0;

public:
    ma_decoder decoder_{};
    ma_uint64 currentFrame_{};
    ma_format format_{};
    ma_uint32 channels_{};
    ma_uint32 sampleRate_{};
    bool decoderInit_ = false;
};

// 本地数据源
class LocalFileSource : public ImplAudioSource {
public:
    explicit LocalFileSource(const std::string& filePath);
    ~LocalFileSource() override;
    ma_uint64 read(void* pOutput, const void* pInput, ma_uint32 frameCount) override;
    ma_result seek(float percent) override;
    AudioSourceType SourceType() const override { return AudioSourceType::LocalFile; }

private:
    std::string filePath_;
    ma_uint64 totalFrames_{};
};

// 网络流数据源
class NetworkStreamSource : public ImplAudioSource {
public:
    explicit NetworkStreamSource(std::shared_ptr<NetworkDownloader> downloader);
    ~NetworkStreamSource() override;
    ma_uint64 read(void* pOutput, const void* pInput, ma_uint32 frameCount) override;
    ma_result seek(float percent) override;
    AudioSourceType SourceType() const override { return AudioSourceType::NetworkStream; }

private:
    std::shared_ptr<NetworkDownloader> downloader_;
    size_t totalLengths_{};
};
