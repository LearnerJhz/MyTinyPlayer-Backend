#include "AudioPlayer.h"
#include "utils/Logger.h"
bool AudioPlayer::setSource(std::unique_ptr<ImplAudioSource> src) {

    // 停止当前设备
    if (ma_device_is_started(&device_)) {
        ma_device_stop(&device_);
    }
    // 反初始化旧设备
    if (deviceInit_) {
        ma_device_uninit(&device_);
        deviceInit_ = false;
    }

    // 设置新的音频源
    source_ = std::move(src);

    // 配置播放设备
    ma_format  format_ = source_->decoder_.outputFormat;
    ma_uint32  channels_ = source_->decoder_.outputChannels;
    ma_uint32  sampleRate_ = source_->decoder_.outputSampleRate;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = format_;
    deviceConfig.playback.channels = channels_;
    deviceConfig.sampleRate = sampleRate_;
    deviceConfig.pUserData = this;
    deviceConfig.dataCallback = AudioPlayer::data_callback;
    deviceConfig.periodSizeInFrames = 256; // 每次拉512帧

    if (ma_device_init(&context_, &deviceConfig, &device_) != MA_SUCCESS) {
        LOG_ERROR("Device init failed!");
        return false;
    }

    // 一定要在这里标记，以防万一前面出错然后错误标记
    deviceInit_ = true;
    return true;
}

void AudioPlayer::play() {
    // 检查设备和解码器是否已初始化
    if (!deviceInit_) {
        LOG_ERROR("错误：设备或解码器未初始化");
        return;
    }

    // 检查设备是否已经在运行
    if (ma_device_is_started(&device_)) {
        LOG_WARN("警告：设备已在播放状态");
        return;
    }

    // 尝试启动设备
    ma_result result = ma_device_start(&device_);
    if (result != MA_SUCCESS) {
        LOG_ERROR("播放失败，错误码：%d", result);
        return;
    }
}

void AudioPlayer::pause() {
    // 只允许暂停已初始化的设备
    if (!deviceInit_) {
        LOG_ERROR("错误：设备未初始化");
        return;
    }

    // 只暂停正在运行的设备
    if (ma_device_is_started(&device_)) {
        ma_device_stop(&device_);
        LOG_INFO("已暂停");
    }
    else {
        LOG_WARN("警告：设备未在运行状态");
    }
}

void AudioPlayer::stop() {
    // 设备校验
    if (deviceInit_) {
        // 无论是否运行都尝试停止（ma_device_stop内部会处理未启动的情况）
        ma_device_stop(&device_);
    }

    // 解码器和资源校验
    if (source_) {
        source_->seek(0);
    }
    else {
        LOG_WARN("警告：解码器或资源未初始化");
    }

    currentFrame_ = 0;
    LOG_INFO("已停止");
}

void AudioPlayer::data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    // 从传进去的this指针里取出来
    auto* player = reinterpret_cast<AudioPlayer*>(pDevice->pUserData);
    if (player->source_) {
        // 加锁确保线程安全（如果在别处修改 source_）
        //std::lock_guard<std::mutex> lock(player->mutex_);

        if (player->source_) {                              // 神人私有成员竟然能被访问
            auto framesRead = player->source_->read(pOutput, nullptr, frameCount);
            // 跳转的话这个得改
            player->currentFrame_ += framesRead;
  
            //// 若读取不足，补 0（避免杂音）
            //if (framesRead < frameCount) {
            //    uint8_t* outputBytes = reinterpret_cast<uint8_t*>(pOutput);
            //    std::memset(outputBytes + framesRead * ma_get_bytes_per_frame(player->format_, player->channels_),
            //        0,(frameCount - framesRead) * ma_get_bytes_per_frame(player->format_, player->channels_));
            //}
        }
    }
}
