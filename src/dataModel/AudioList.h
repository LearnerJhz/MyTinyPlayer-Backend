#pragma once
#include <string>
#include <vector>
#include "AudioSourceType.h"

//一首歌的元信息
struct AudioMeta {
    std::string title;         // 歌名
    std::string artist;     // 艺术家
    std::string album;      // 专辑
    std::string coverURL; // 图片的哈希文件名
    double duration;        // 时长
    size_t size;
};
// 一首歌的元信息 + 播放源
struct AudioTrack {
    AudioMeta meta;
    std::string sourceURL; // 本地路径 / 网络URL
    AudioSourceType sourceType;
    std::string trackId;  // 唯一标识
    bool liked = false;
};

// 歌单组织
struct AudioList {
    std::string name;
    std::vector<AudioTrack> tracks;
};

class AudioList {
public:
    AudioList(const std::string& name) : name_(name) {}
    const std::string& name() const { return name_; }
    void rename(const std::string& newName) { name_ = newName; }

    // 添加一个 AudioTrack
    void addTrack(const AudioTrack& track) {
        if (!hasTrack(track.trackId)) {
            tracks_.push_back(track);
        }
    }

    // 移除一个 AudioTrack
    bool removeTrackById(const std::string& trackId) {
        auto it = std::remove_if(tracks_.begin(), tracks_.end(),
            [&](const AudioTrack& t) { return t.trackId == trackId; });
        if (it != tracks_.end()) {
            tracks_.erase(it, tracks_.end());
            return true;
        }
        return false;
    }

    // 获取只读访问
    const std::vector<AudioTrack>& tracks() const { return tracks_; }

    // 判断是否包含某 trackId
    bool hasTrack(const std::string& trackId) const {
        return std::any_of(tracks_.begin(), tracks_.end(),
            [&](const AudioTrack& t) { return t.trackId == trackId; });
    }

private:
    std::string name_;
    std::vector<AudioTrack> tracks_;
};


class ImplAudioListService {
public:
    virtual ~ImplAudioListService() = default;

    virtual std::vector<std::string> listAudioLists() const = 0;
    virtual bool saveAudioList(const AudioList&) = 0;
    virtual std::optional<AudioList> loadAudioList(const std::string& name) const = 0;
    virtual bool deleteAudioList(const std::string& name) = 0;
private:
    AudioList list;
};

class AudioListSyncService {
public:
    AudioListSyncService(std::shared_ptr<ImplAudioListService> local,
        std::shared_ptr<ImplAudioListService> cloud)
        : local_(std::move(local)), cloud_(std::move(cloud)) {}

    bool save(const AudioList& list) {
        // 保存到本地
        bool localOk = local_->saveAudioList(list);

        // 同步策略：如果登录则同步
        if (cloud_) {
            cloud_->saveAudioList(list);  // 可以改成异步或失败容忍
        }
        return localOk;
    }

    std::optional<AudioList> load(const std::string& name) {
        auto localList = local_->loadAudioList(name);
        if (localList) return localList;

        // 如果本地没有，从云拉取并回写到本地
        if (cloud_) {
            auto cloudList = cloud_->loadAudioList(name);
            if (cloudList) {
                local_->saveAudioList(*cloudList);  // 回写
                return cloudList;
            }
        }
        return std::nullopt;
    }

    void syncToCloud() {
        if (!cloud_) return;
        for (const auto& name : local_->listAudioLists()) {
            auto list = local_->loadAudioList(name);
            if (list) cloud_->saveAudioList(*list);
        }
    }

private:
    std::shared_ptr<ImplAudioListService> local_;
    std::shared_ptr<ImplAudioListService> cloud_; // 可选启用
};



class AudioLibrary {
public:
    const std::vector<AudioList>& getAllLists() const;
    const AudioTrack* findTrackById(const std::string& id) const;

    void addTrackToList(const AudioTrack& track, const std::string& listName);
    void likeTrack(const std::string& trackId, bool liked = true);

private:
    std::vector<AudioList> lists_;
    std::unordered_map<std::string, AudioTrack*> trackMap_;
};
