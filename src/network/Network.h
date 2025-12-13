#pragma once
#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <thread>
#include <chrono>
#include <fstream>
#include <unordered_set>
#include <string_view>
#include <optional>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include "utils/Macros.h"
#include "utils/Logger.h"
using asio::ip::tcp;
namespace ssl = asio::ssl;

// 配置参数示例
constexpr size_t MaxConnections = 4;       // 最大并行连接数
constexpr size_t MaxBufferSize = 16 * 1024 * 1024; // 16MB全局缓冲
constexpr size_t ConnectionTTL = 5;      // 空闲连接保留时间


//--------------------- URL解析结构体 ---------------------
struct ParsedUrl {
    std::string host;
    std::string port = "443";
    std::string path = "/";
    std::string filename;  // 新增的输出文件名字段
};

std::string extractHost(const std::string& url);
ParsedUrl parseUrl(const std::string& url);


//--------------------- HTTP响应解析 ---------------------

static constexpr size_t block_ = 1024 * 256; // 每次申请字节
struct HttpResponse {
    std::string content_type;
    size_t content_length = 0;
    int status_code = 0;
    bool is_chunked = false;
    bool is_partial = false;

    // 重置函数
    void reset() {
        content_type.clear(); // 重置 std::string
        content_length = 0;
        status_code = 0;
        is_chunked = false;
        is_partial = false;
    }
};

struct RangeBlock {
    size_t range_start = 0;
    size_t range_end = block_ - 1;
    size_t content_length = 0;
    size_t total_length = 0;

    // 重置函数
    void reset() {
        range_start = 0;
        range_end = block_ - 1;
        content_length = 0;
        total_length = 0;
    }

    // 更新到下一块
    void moveToNextBlock() {
        content_length = range_end - range_start + 1;
        range_start = range_end + 1;
        range_end = (std::min)(range_start + block_ - 1, total_length - 1);
    }
};

struct DummyLock {
    void lock() noexcept {}
    void unlock() noexcept {}
    bool try_lock() noexcept { return true; }

    DummyLock() = default;
    DummyLock(const DummyLock&) = delete;
    DummyLock& operator=(const DummyLock&) = delete;
    DummyLock(DummyLock&&) noexcept = default;
    DummyLock& operator=(DummyLock&&) noexcept = default;
};




//改进的小“下载器”（支持连接复用）,被Source持有，相当于下载功能
class NetworkDownloader : public std::enable_shared_from_this<NetworkDownloader> {
public:
    NetworkDownloader(asio::io_context& io, ssl::context& ctx, const std::string& url);
    ~NetworkDownloader();   // 通知所有等待线程（避免死锁）
    size_t totalLength() { return rangeBlock_.total_length; }
    bool isEndOfStream() { return isEnd; }
    bool matchHost(const std::string& url) { return parsedUrl_.host == extractHost(url); }
    bool isReusable() const { return socket_.next_layer().is_open(); }  // 或更复杂策略，如状态标志
    void updateLastUsedTime() { lastUsed_ = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point lastUsedTime() const { return lastUsed_; }

    void reset() {
        httpResponse_.reset();
        rangeBlock_.reset();
        ringBuffer_.clear();
        readPos_ = 0;
        writePos_ = 0;
        size_ = 0;
    }
    // 启动异步下载流程 DNS解析
    void start();
    void reconnect();
    bool seek(size_t pos) {
        // 有缓存的情况
        if (bufferStartOffset_ < pos) {
            readPos_ = pos;
            return true;
        }
        
        pendingSeekPos_.store(pos, std::memory_order_relaxed);
        return false;
    }
    void waitUntilBuffered(size_t requiredBytes) {
        LOG_INFO("wait for buffer...");
        std::unique_lock<std::mutex> lock(bufferMutex_);
        dataAvailable_.wait(lock, [&]() {
            return size_ >= requiredBytes;
            });
        LOG_INFO("CV awake ,buffered!");

    }
   
private:
    // 使用 string_view 包装字符数组
    static constexpr std::string_view CommonHeaders_ =
        "Connection: Keep-Alive\r\n"
        "User-Agent: MyMusicPlayer\r\n"
        "Accept: */*\r\n"
        "Upgrade: none\r\n";          // 明确拒绝升级

    static constexpr size_t BufferCapacity_ = 4 * 1024 * 1024; // 4MB 缓冲区
    static constexpr size_t BufferPrefetch_ = 2 * 1024 * 1024; // 2MB 预读
    void sendRangeRequest(); //start和end在rangeBlock里
    void ParseHeaders();
    bool isStopPrefetch() const {
        return size_ > BufferPrefetch_;
    }

    
    void asyncReadRangeBody() {
        const size_t expected_size = rangeBlock_.content_length;

        asio::async_read(socket_, buffer_, asio::transfer_exactly(expected_size),
            [self = shared_from_this(), expected_size](const asio::error_code& ec, size_t bytes_transferred) {
                if (ec && ec != asio::error::eof) {
                    DEBUG_CERR << "读取 range 数据出错: " << ec.message() << std::endl;
                    return;
                }
                DEBUG_COUT << "预期读取: " << expected_size << " 字节，实际读取: " << bytes_transferred << " 字节\n";
                DEBUG_COUT << "读完后buffersize: " << self->buffer_.size() << "\n";

                // 将数据写入环形缓冲区  {}限制锁的作用域 RAII  防止启动多个线程运行 io_context 线程安全
                {
                    //std::unique_lock<std::mutex> lock(self->bufferMutex_);
                    
                    //计算缓冲区当前可写入的空间
                    size_t available = BufferCapacity_ - self->size_ - 1;

                    if (expected_size > available) {
                        // 缓冲区溢出处理
                        std::cerr << "Buffer overflow!" << std::endl;
                        return;
                    }
                    // 直接写入（无需绕到头部）
                    if (self->writePos_ + expected_size <= BufferCapacity_) {
                        std::copy(
                            asio::buffers_begin(self->buffer_.data()),
                            asio::buffers_end(self->buffer_.data()),
                            self->ringBuffer_.begin() + self->writePos_ 
                        );
                    }
                    // 分两段写入（尾部到末尾 + 头部剩余部分）
                    else {
                        size_t firstChunk = BufferCapacity_ - self->writePos_;
                        std::copy(
                            asio::buffers_begin(self->buffer_.data()),
                            asio::buffers_begin(self->buffer_.data()) + firstChunk,
                            self->ringBuffer_.begin() + self->writePos_); // 目标起始位置
                        std::copy(
                            asio::buffers_begin(self->buffer_.data()) + firstChunk,
                            asio::buffers_begin(self->buffer_.data()) + expected_size,
                            self->ringBuffer_.begin());                   // 目标起始位置
                    }
                    // 更新写的位置，当前位置是没有数据的
                    self->writePos_ = (self->writePos_ + expected_size) % BufferCapacity_;
                    //DEBUG_COUT << "writePos_ 位置 " << self->writePos_ << std::endl;

                    self->size_ += expected_size;
                }
                    LOG_INFO("ringbuffer size_%zu",self->size_);
                    self->dataAvailable_.notify_one(); // 通知可以初始化了

                // 文件先不写
                //file.write(reinterpret_cast<char*>(self->ringBuffer_.data()), expected_size);

                self->buffer_.consume(expected_size); // 安全的，超过size也没事
                //std::this_thread::sleep_for(std::chrono::milliseconds(7000));

                self->reconnect();
            });
    }

    void checkSeekRange(); // 用于网络请求不同的range

    void startHeartbeat();
    void sendHeartbeat();

    void asyncConnect(const tcp::resolver::results_type& endpoints);// 尝试连接端点
    void sslHandShake(); // SSL握手
    void sendHttpRequest();

    // 处理分块的，这里有下载，待解耦
    void asyncReadChunks(std::ofstream& file);
    void asyncReadChunkData(size_t chunk_size, std::ofstream& file);

public:
    // 从缓冲区读取数据（供播放器调用）
    size_t readBuffer(void* dest, size_t requestSize) {
        // 这个是为了防止decoder拉不到数据然后停下来
        {
            // 如果下载失败或 shutdown() 后没有触发 notify_one()，这个地方会永远卡住 可加入超时与退出机制
            std::unique_lock<std::mutex> lock(bufferMutex_);
            dataAvailable_.wait(lock, [&]() {
                return size_ > 0;
                });
        }
        // 计算可读数据量
        size_t readSize = (std::min)(requestSize, size_);

        // 执行拷贝
        uint8_t* byteDest = static_cast<uint8_t*>(dest);  // 将 void* 转为 uint8_t*

        if (readPos_ + readSize <= BufferCapacity_) {
            std::copy(ringBuffer_.begin() + readPos_,
                ringBuffer_.begin() + readPos_ + readSize,
                byteDest);  // 拷贝数据到目标缓冲区
        }
        else {
            size_t firstChunk = BufferCapacity_ - readPos_;
            std::copy(ringBuffer_.begin() + readPos_,
                ringBuffer_.end(),
                byteDest);  // 拷贝第一部分数据
            std::copy(ringBuffer_.begin(),
                ringBuffer_.begin() + (readSize - firstChunk),
                byteDest + firstChunk);  // 拷贝第二部分数据
        }

        readPos_ = (readPos_ + readSize) % BufferCapacity_;
        size_ -= readSize;

        return readSize;
    }

    // 关闭连接
    void shutdown() {
        std::cout << "~socket shut down!" << '\n';
        //std::unique_lock<std::mutex> lock(bufferMutex_);
        active_ = false;
        asio::error_code ec;
        socket_.shutdown(ec); // 安全关闭SSL

        // 2. 确保底层socket关闭
        socket_.lowest_layer().close(ec);
        if (ec) {
            std::cerr << "Socket close error:" << ec;
        }
    }

private:
    friend class NetworkStreamSource;
    std::ofstream outFile;
    bool notFirstParse = false;             // 第一次解析，供初始化contextlength用
    bool isEnd = false;                     // 标记流是否传输完了
    std::atomic<std::optional<size_t>> pendingSeekPos_; // C++17 optional，也可以自己用标志

    std::atomic<bool> isPaused_ = false;    // 控制readProc是否暂停
    std::mutex pauseMutex_;
    std::condition_variable pauseCV_;


    // ASIO核心
    asio::io_context& ioContext_;
    ssl::context& sslContext_;
    ssl::stream<tcp::socket> socket_;    //SSL/TLS 加密流，基于一个底层的 TCP 套接字（tcp::socket）
    asio::streambuf buffer_;

    asio::steady_timer heartbeatTimer_;

    std::string url_;
    ParsedUrl parsedUrl_;
    HttpResponse httpResponse_;
    RangeBlock rangeBlock_;
    std::vector<uint8_t> ringBuffer_{}; // 在构造函数预分配 4MB 空间

    size_t bufferStartOffset_ = 0;      // ringBuffer 中的起始位置（全局文件偏移）
    size_t readPos_ = 0;                // 数据拿走的位置
    size_t writePos_ = 0;               // 数据 通过async_read_some() 写入的为主
    size_t size_ = 0;                   // 现有数据量
    bool downloading_ = false;          // 得考虑好是不是下载本地文件
    std::chrono::steady_clock::time_point lastUsed_ = std::chrono::steady_clock::now();

    LockType bufferMutex_;
    std::condition_variable dataAvailable_;
    std::atomic<bool> active_{ true };  // 用来标记是不是活跃的连接，和空闲连接做区分，给Mgr优化用
};

class NetworkDownloadMgr {
public:
    // 删除拷贝构造/赋值
    NetworkDownloadMgr(const NetworkDownloadMgr&) = delete;
    NetworkDownloadMgr& operator=(const NetworkDownloadMgr&) = delete;
    static NetworkDownloadMgr& getInstance() { static NetworkDownloadMgr instance; return instance; }
    asio::io_context& getIoContext() { return ioContext_; }
    ssl::context& getSslContext() { return sslContext_; }// 获取 SSL 上下文

    // 核心接口：获取下载器（线程安全）
    std::shared_ptr<NetworkDownloader> getDownloader(const std::string& url) {

        // 是否会异步地从多个线程提交任务？先加锁，，后续再看
        std::lock_guard lock(poolMutex_);

        // 尝试复用连接
        auto it = std::find_if(idleConnections_.begin(), idleConnections_.end(),
            [&url](std::shared_ptr<NetworkDownloader>& conn) {

                return conn->matchHost(url) && conn->isReusable();
            });

        if (it != idleConnections_.end()) {
            auto& conn = *it;
            idleConnections_.erase(it);
            activeConnections_.insert(conn);
            return conn;
        }

        // 创建新下载器
        auto newConn = std::make_shared<NetworkDownloader>(ioContext_, sslContext_, url);
        activeConnections_.insert(newConn);
        return newConn;
    }

    // 释放下载器（线程安全）
    void releaseDownloader(std::shared_ptr<NetworkDownloader> conn) {
        std::lock_guard lock(poolMutex_);

        // erase(conn)将其从活跃集合中移除.如果直接调用close()。但此时连接可能还在activeConnections_中
        if (activeConnections_.erase(conn)) {
            if (conn->isReusable()) {
                conn->updateLastUsedTime(); // 更新时间
                idleConnections_.push_front(conn);
            }
            else {
                conn->shutdown();
            }
        }
    }

private:
    // 用于清理空闲连接的时间
    static constexpr auto IDLE_TIMEOUT = std::chrono::seconds(30);

    // 清理过期连接 ,在构造函数中就调用，每隔1秒 
    void cleanupExpiredConnections() {
        std::lock_guard lock(poolMutex_);
        auto now = std::chrono::steady_clock::now();

        // 清理空闲连接（超过 IDLE_TIMEOUT 的连接）
        idleConnections_.remove_if([now](const std::shared_ptr<NetworkDownloader>& conn) {
            return (now - conn->lastUsedTime()) > IDLE_TIMEOUT;
            });
    }

    NetworkDownloadMgr()
        : ioContext_(), 
        sslContext_(ssl::context::tlsv12_client),
        workGuard_(asio::make_work_guard(ioContext_)){
        // 初始化 SSL 上下文
        SSL_CTX_set_options(sslContext_.native_handle(),
            SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);

        //sslContext_.set_verify_mode(ssl::verify_peer); // 必须添加
        sslContext_.set_default_verify_paths();

        // 启动IO线程
        ioThread_ = std::thread([this] {
            ioContext_.run();
            });

        //// 启动连接清理线程 若其他线程操作可能不安全（当前有锁保护，但需确认）
        //cleanupThread_ = std::thread([this] {
        //    while (running_) {
        //        cleanupExpiredConnections();
        //        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        //    }
        //    });

    }
    ~NetworkDownloadMgr() {
        running_ = false;
        workGuard_.reset(); // 允许io_context自然停止
        ioContext_.stop();  // 取消所有未完成操作

        // 清理 停止所有下载任务？保存不？？
        //如何确保所有异步操作都被正确取消？例如，
        // 如果还有正在执行的下载任务，直接停止ioContext_可能导致问题
        
        //在析构时，是否需要释放workGuard_以允许ioContext_.run()退出？
        //    因为析构时会先销毁workGuard_（成员变量析构顺序与声明顺序相反
        //    ，workGuard_在ioContext_和sslContext_之后），当workGuard_被销毁时，
        //    ioContext_会停止，因此ioThread_.join()应该能够正确结束

        //join清理线程。之后join ioThread_
        
        //cleanupThread_.join();
        if (ioThread_.joinable()) ioThread_.join();
    }

private:

    // ASIO核心
    asio::io_context ioContext_;        // 共享的 io_context
    ssl::context sslContext_;           // SSL 上下文
    using ExecutorWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;
    ExecutorWorkGuard workGuard_;       // 保持ioContext_一直运行，避免在没有任务时退出

    // 线程控制
    std::atomic<bool> running_{ true }; // 原子变量控制清理线程的运行
    std::thread ioThread_;              /* 运行ioContext_的处理循环
                                            可能成为性能瓶颈。如果下载任务很多，
                                             单个IO线程可能无法处理所有请求，应考虑使用多个线程运行io_context*/
    std::thread cleanupThread_;         // 定期清理空闲连接

    // 连接池
    std::mutex poolMutex_; //保护连接池的访问
    std::list<std::shared_ptr<NetworkDownloader>> idleConnections_;  // 空闲连接池
    std::unordered_set<std::shared_ptr<NetworkDownloader>> activeConnections_;  // 活跃连接池
};
