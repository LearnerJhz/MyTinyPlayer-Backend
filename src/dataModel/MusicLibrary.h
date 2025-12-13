//#pragma once
//#include <vector>
//#include <memory>
//#include <string>
//#include "Playlist.h"
////统一管理歌单资源：
////用户的“媒体资源中心”，管理：
////
////收藏夹、历史、推荐、下载记录等多个 Playlist
////
////提供搜索、添加到收藏等逻辑
//class MusicLibrary {
//public:
//    std::vector<std::shared_ptr<AudioList>> userPlaylists;
//    std::shared_ptr<AudioList> likedPlaylist;
//
//    std::shared_ptr<AudioList> createPlaylist(const std::string& name);
//    void deletePlaylist(const std::string& name);
//};