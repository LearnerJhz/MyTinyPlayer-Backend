#pragma once
#include "PlayList.h"
#include "AudioPlayer.h"
//控制当前播放索引、播放模式、切歌等
//不管理收藏 / 数据存储，只引用歌单 处理播放状态 + 当前歌单）
//控制器可以暴露给 UI 做所有播放逻辑管理，比如
//点一首歌播放
//点下一首
//点喜欢
//切换播放列表
//AudioController 是“控制当前播放状态”，协调播放器 + 歌单 + 播放逻辑
class AudioController {
    AudioPlayer player;
    std::shared_ptr<AudioList> currentPlaylist;
    int currentIndex = -1;

public:
    void setPlaylist(std::shared_ptr<AudioList> playlist);
    void playAtIndex(int index);
    void playNext();
    void playPrev();
    void toggleLike();
    const AudioTrack* getCurrentTrack() const;
};