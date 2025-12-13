#pragma once
#include <memory>
#include <mutex>
#include "miniaudio.h"
#include "source/ImplAudioSource.h"

class AudioPlayer {
public:
    AudioPlayer() { ma_context_init(NULL, 0, NULL, &context_); }
    ~AudioPlayer() { ma_device_uninit(&device_); ma_context_uninit(&context_); }
    bool setSource(std::unique_ptr<ImplAudioSource> src);
    void play();    
    void pause();  
    void stop();    //  put frame=0 安全校验

    //  t * samplerates不太准，percent*totalFrames好一点
    void seek(float percent) { if (source_)source_->seek(percent); }
    double getCurrentTime() const { return static_cast<double>(currentFrame_); }
    AudioSourceType SourceType() const { return source_->SourceType(); }

private:
    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

private:
    //  唯一设备成员
    ma_context context_;
    ma_device device_{};

    // 持有资源
    std::unique_ptr<ImplAudioSource> source_;
    std::mutex mutex_;

    // 变量的track，便于内部调用
    bool deviceInit_ = false;
    ma_uint64 currentFrame_{};
};
