#pragma once

#include <string>
#include <stdexcept>

#include <string>
#include <stdexcept>  // 标准异常处理库，提供标准异常类如out_of_range

// 表示单个音轨的结构体（最小数据单元）
struct Track {
    std::string path; // 音频文件的完整路径

    // 构造函数：使用路径初始化音轨
    Track(const std::string& p) : path(p) {}
};

// 播放列表类：基于动态数组实现的音轨容器
class Playlist {
private:
    Track** tracks_;    // 指向Track指针数组的指针（二级指针）
    int size_;          // 当前存储的音轨数量
    int capacity_;      // 当前数组的容量（可容纳的音轨数量）

    // 调整内部数组大小（核心扩容/缩容机制）
    void resize(int newCapacity) {
        // 创建新容量的指针数组
        Track** newTracks = new Track * [newCapacity];

        // 将现有音轨指针复制到新数组
        for (int i = 0; i < size_; ++i) {
            newTracks[i] = tracks_[i]; // 仅复制指针（浅拷贝），不复制Track对象本身
        }

        // 释放旧数组内存
        delete[] tracks_;

        // 更新指针和容量
        tracks_ = newTracks;
        capacity_ = newCapacity;
    }

public:
    // 构造函数：初始化空播放列表
    Playlist() : tracks_(nullptr), size_(0), capacity_(0) {}

    // 拷贝构造函数：深拷贝另一个播放列表
    Playlist(const Playlist& other)
        : tracks_(new Track* [other.capacity_]),
        size_(other.size_),
        capacity_(other.capacity_)
    {
        // 深拷贝每个音轨对象
        for (int i = 0; i < size_; ++i) {
            tracks_[i] = new Track(*other.tracks_[i]);
        }
    }

    // 拷贝赋值运算符：处理对象赋值时的深拷贝
    Playlist& operator=(const Playlist& other) {
        // 防止自赋值
        if (this != &other) {
            // 释放当前对象的所有资源
            for (int i = 0; i < size_; ++i) {
                delete tracks_[i];  // 删除每个Track对象
            }
            delete[] tracks_;       // 删除指针数组

            // 从other对象复制数据（深拷贝）
            tracks_ = new Track * [other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            for (int i = 0; i < size_; ++i) {
                tracks_[i] = new Track(*other.tracks_[i]); // 创建新Track副本
            }
        }
        return *this;
    }

    // 析构函数：清理所有动态分配的内存
    ~Playlist() {
        // 删除每个Track对象
        for (int i = 0; i < size_; ++i) {
            delete tracks_[i];
        }
        // 删除指针数组
        delete[] tracks_;
    }

    // 添加音轨到列表末尾
    void addTrack(const std::string& path) {
        // 检查是否需要扩容
        if (size_ == capacity_) {
            // 初始容量为4，后续每次扩容为当前容量的两倍
            int newCapacity = (capacity_ == 0) ? 4 : capacity_ * 2;
            resize(newCapacity);
        }
        // 创建新Track对象并添加到数组
        tracks_[size_++] = new Track(path);
    }

    // 移除指定索引处的音轨
    void removeTrack(int index) {
        // 边界检查（使用stdexcept的异常）
        if (index < 0 || index >= size_) {
            throw std::out_of_range("Index out of range");
        }

        // 删除目标音轨对象
        delete tracks_[index];

        // 向前移动后续元素（覆盖被删除的位置）
        for (int i = index; i < size_ - 1; ++i) {
            tracks_[i] = tracks_[i + 1];
        }

        // 更新大小
        --size_;
    }

    // 获取指定索引处的音轨（常量引用）
    const Track& getTrack(int index) const {
        // 边界检查
        if (index < 0 || index >= size_) {
            throw std::out_of_range("Index out of range");
        }
        return *tracks_[index]; // 返回解引用的Track对象
    }

    // 获取当前音轨数量
    int size() const {
        return size_;
    }

    // 检查播放列表是否为空
    bool isEmpty() const {
        return size_ == 0;
    }

    // 清空播放列表（释放所有资源）
    void clear() {
        // 删除每个Track对象
        for (int i = 0; i < size_; ++i) {
            delete tracks_[i];
        }
        // 删除指针数组
        delete[] tracks_;

        // 重置状态
        tracks_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }
};