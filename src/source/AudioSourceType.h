#pragma once
// 音频来源类型,留了一个扩展口
enum class AudioSourceType {
    LocalFile,
    NetworkStream,
    Custom
};