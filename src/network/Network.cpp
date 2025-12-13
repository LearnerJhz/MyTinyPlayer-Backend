#include "Network.h"
#include "utils/Logger.h"
std::string extractHost(const std::string& url) {
    // 1. 查找 "://" 确定协议头结束位置
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
        protocol_end = 0;  // 无协议头，从头开始
    }
    else {
        protocol_end += 3;  // 跳过 "://"
    }

    // 2. 提取 host 部分（到第一个 '/', '?', '#', 或 ':'）
    size_t host_start = protocol_end;
    size_t host_end = url.find_first_of("/:?#", host_start);
    if (host_end == std::string::npos) {
        host_end = url.length();
    }

    return url.substr(host_start, host_end - host_start);
}

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;
    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos) {
        throw std::invalid_argument("Invalid URL format");
    }

    std::string remaining = url.substr(protocol_pos + 3);
    size_t path_pos = remaining.find('/');
    if (path_pos != std::string::npos) {
        result.path = remaining.substr(path_pos);
        // 从路径中提取文件名
        size_t last_slash_pos = result.path.find_last_of('/');
        if (last_slash_pos != std::string::npos) {
            result.filename = result.path.substr(last_slash_pos + 1);
        }
        remaining = remaining.substr(0, path_pos);
    }

    size_t port_pos = remaining.find(':');
    if (port_pos != std::string::npos) {
        result.port = remaining.substr(port_pos + 1);
        remaining = remaining.substr(0, port_pos);
    }

    result.host = remaining;

    // 如果没有文件名（比如路径以/结尾），使用默认文件名
    if (result.filename.empty()) {
        result.filename = "output";
    }

    return result;
}

NetworkDownloader::NetworkDownloader(asio::io_context& io, ssl::context& ctx, const std::string& url) : ioContext_(io),                   // 初始化IO上下文引用
sslContext_(ctx),                 // 初始化SSL上下文引用
socket_(ioContext_, sslContext_), // 创建SSL流（底层TCP socket未打开）
heartbeatTimer_(ioContext_),        // 心跳请求
url_(url),                        // 存储原始URL
ringBuffer_(BufferCapacity_),      // 分配 4MB 空间
active_(true)                     // 标记为活跃状态
{

    //解析URL parseUrl是外部函数
    parsedUrl_ = parseUrl(url_);

    // 验证URL解析结果
    if (parsedUrl_.host.empty()) {
        std::cerr << "Invalid URL format: " << url << '\n';
    }

    // 在 TLS 握手阶段告诉服务器要访问的域名（通过 SNI 扩展），确保服务器返回正确的证书
    bool flag = SSL_set_tlsext_host_name(socket_.native_handle(), parsedUrl_.host.c_str());
    if (!flag) {
        std::cerr << "Failed to set SNI hostname " << '\n';
    }


    // 配置SSL验证模式（可选）在Mgr里设置的?
    // socket_.set_verify_mode(ssl::verify_peer);
    // socket_.set_verify_callback(...);
}

NetworkDownloader::~NetworkDownloader() {
    // 1. 标记为不活跃，停止所有异步操作
    active_.store(false, std::memory_order_release);

    // 2. 安全关闭连接
    asio::error_code ec; // 忽略错误避免抛出异常
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);

        // 关闭SSL流
        if (socket_.lowest_layer().is_open()) {
            socket_.shutdown(ec); // 发送SSL关闭通知
            if (ec && ec != asio::error::eof) {
                std::cerr << "SSL Shutdown error: " << ec.message() << std::endl;
            }

            // 关闭底层TCP套接字
            socket_.lowest_layer().close(ec);
            if (ec) {
                std::cerr << "Socket close error: " << ec.message() << std::endl;
            }
        }
    }

    // 3. 通知所有等待线程（避免死锁）
    //dataAvailable_.notify_all();
}

void NetworkDownloader::start() {

    LOG_INFO("Async resolve...");
    if (!active_) return;

    // 异步DNS解析 resolver = std::move(resolver)
    auto resolver = std::make_shared<tcp::resolver>(ioContext_);
    resolver->async_resolve(parsedUrl_.host, parsedUrl_.port,
        [self = shared_from_this(), resolver](const asio::error_code& ec, tcp::resolver::results_type endpoints) {
            if (ec || !self->active_) {
                std::cerr << "Resolve failed: " << ec.message() << std::endl;
                return;
            }
            self->asyncConnect(endpoints);
        });
}

void NetworkDownloader::reconnect() {
    asio::error_code ec;
    // 关闭旧的 socket，保险起见
    socket_.lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
    socket_.lowest_layer().close(ec);

    // 清空 buffer 避免残留数据影响后续
    buffer_.consume(buffer_.size());
    DEBUG_COUT << "reconnect 后buffer大小" << buffer_.size() << std::endl;
    // 在原位置直接新建一个 socket对象
    socket_ = ssl::stream<tcp::socket>(ioContext_, sslContext_);

    // 在 握手阶段告诉服务器要访问的域名，确保服务器返回正确的证书
    SSL_set_tlsext_host_name(socket_.native_handle(), parsedUrl_.host.c_str());
    start();
}

void NetworkDownloader::sendRangeRequest() {
    checkSeekRange(); // 更新rangBlock里的 start 和 end

    ////计算缓冲区当前可写入的空间
    //size_t available = BufferCapacity_ - size_ - 1;
    //if ((std::min)(block_, rangeBlock_.total_length-rangeBlock_.range_start) > available) {
    //    // 缓冲区溢出处理
    //    std::cerr << "Buffer overflow!" << std::endl;
    //    return;
    //}

    // 使用 ostringstream 高效拼接
    std::ostringstream request;
    request << "GET " << parsedUrl_.path << " HTTP/1.1\r\n"
        << "Host: " << parsedUrl_.host << "\r\n"
        << "Range: bytes=" << rangeBlock_.range_start << "-" << rangeBlock_.range_end << "\r\n"
        << CommonHeaders_ << "\r\n";  // 插入公共头

    rangeBlock_.moveToNextBlock(); // 更新信息，以后seek网络流的时候直接改block

    // 异步发送请求
    asio::async_write(socket_, asio::buffer(request.str()),
        [self = shared_from_this()](const asio::error_code& ec, size_t /*bytes*/) {
            if (ec || !self->active_) {
                std::cerr << "Send HTTP request failed: " << ec.message() << std::endl;
                self->shutdown();
                return;
            }
            self->ParseHeaders();
        });

}

void NetworkDownloader::ParseHeaders() {
    asio::async_read_until(socket_, buffer_, "\r\n\r\n",
        [self = shared_from_this()](const asio::error_code& ec, size_t bytes_transferred) {
            DEBUG_COUT << "准备解析头部，此时buffersize " << self->buffer_.size() << std::endl;
            if (ec) {
                if (!self->socket_.lowest_layer().is_open()) {
                    std::cout << "连接关闭" << std::endl << std::endl;;
                }
                std::cerr << "Read error (header): " << ec.message() << std::endl;
                return;
            }

            //// 根据标记来办事，要是
            //if (self->notFirstParse && self->httpResponse_.is_partial) {
            //    self->asyncReadRangeBody();
            //}

            // 使用一个 std::istream 来从 buffer_ 中读取数据
            std::istream header_stream(&self->buffer_);
            std::string status_line;
            std::getline(header_stream, status_line);

            DEBUG_COUT << "响应状态行: " << status_line << "\n";

            // 新增状态码解析（极简版）
            size_t code_start = status_line.find(' ') + 1;    // 找到第一个空格后
            self->httpResponse_.status_code = std::stoi(status_line.substr(code_start, 3)); // 直接截取3位数字

            std::string line;
            while (std::getline(header_stream, line) && line != "\r") {
                DEBUG_COUT << "响应头部: " << line << "\n";

                // 这个content_length 是为了chunk传输用的
                if (line.find("Content-Length:") == 0) {
                    self->httpResponse_.content_length = std::stoull(line.substr(15));
                }
                else if (line.find("Content-Type:") == 0) {
                    self->httpResponse_.content_type = line.substr(13);
                    self->httpResponse_.content_type.pop_back(); // 去掉最后的 '\r' 或 '\n'
                }
                else if (line.find("Transfer-Encoding: chunked") != std::string::npos) {
                    self->httpResponse_.is_chunked = true;
                }
                // 总长度
                else if (line.find("Content-Range:") == 0) {
                    self->httpResponse_.is_partial = true;

                    // 直接定位到 '/' 符号提取 total_length
                    size_t slashPos = line.find('/');
                    if (slashPos != std::string::npos) {
                        try {
                            // 截取 '/' 后的部分并转换为数值
                            std::string totalStr = line.substr(slashPos + 1);
                            self->rangeBlock_.total_length = std::stoull(totalStr);
                        }
                        catch (const std::exception& e) {
                            std::cerr << "Failed to parse Content-Range total_length: " << e.what() << std::endl;
                        }
                    }
                }
            }

            // 打印调试
            DEBUG_COUT << "状态码: " << self->httpResponse_.status_code
                << ", Content-Length: " << self->httpResponse_.content_length
                << ", Content-Type: " << self->httpResponse_.content_type << "\n";

            //// 打开文件
            //self->outFile.open("output.mp3", std::ios::binary);
            //if (!self->outFile.is_open()) {
            //    std::cerr << "Failed to open file!" << std::endl;
            //    // 处理错误情况
            //}

            // 兼容chunk逻辑处理
            if (self->httpResponse_.is_chunked) {
                DEBUG_COUT << "Chunked transfer encoding detected.\n";
                self->asyncReadChunks(self->outFile);
            }

            // 接着处理 body（可能已经部分读入 buffer_）
            else if (self->httpResponse_.is_partial) {
                self->asyncReadRangeBody();
            }
        });
}

void NetworkDownloader::checkSeekRange() {
    // 检查是否有等待的seek请求
    auto pending = pendingSeekPos_.load(std::memory_order_relaxed);
    if (pending.has_value()) {
        // 应用seek
        rangeBlock_.range_start = pending.value();
        rangeBlock_.range_end = pending.value() + block_ - 1;
        // 清除pending
        pendingSeekPos_.store(std::nullopt, std::memory_order_relaxed);
    }
}

void NetworkDownloader::startHeartbeat() {
    heartbeatTimer_.expires_after(std::chrono::seconds(2));
    heartbeatTimer_.async_wait([self = shared_from_this()](const asio::error_code& ec) {
        try {
            if (!ec) {
                self->sendHeartbeat();
                self->startHeartbeat();
            }
            else {
                DEBUG_CERR << "Heartbeat timer cancelled or error: " << ec.message() << std::endl;
            }
        }
        catch (const std::exception& ex) {
            DEBUG_CERR << "Exception in heartbeat callback: " << ex.what() << std::endl;
        }
        });

}

void NetworkDownloader::sendHeartbeat() {
    // 使用 ostringstream 高效拼接请求头
    std::ostringstream request;
    request << "GET " << parsedUrl_.path << " HTTP/1.1\r\n"
        << "Host: " << parsedUrl_.host << "\r\n"
        << "Connection: Keep-Alive\r\n"  // 保持连接活跃
        << "User-Agent: MyMusicPlayer/1.0\r\n"  // 自定义User-Agent
        << "\r\n";  // 空请求体

    // 直接发送心跳请求
    if (socket_.next_layer().is_open()) {
        asio::write(socket_, asio::buffer(request.str()));
    }
    else {
        DEBUG_CERR << "SSL Socket is not open!" << std::endl;
    }
}

void NetworkDownloader::asyncConnect(const tcp::resolver::results_type& endpoints) {
    LOG_INFO("Async connect...");
    asio::async_connect(socket_.next_layer(), endpoints,
        [self = shared_from_this()](const asio::error_code& ec, const tcp::endpoint&) {
            if (ec || !self->active_) {
                std::cerr << "Connect failed: " << ec.message() << std::endl;
                return;
            }
            self->sslHandShake();
        });
}

void NetworkDownloader::sslHandShake() {
    LOG_INFO("SSL handShake...");
    socket_.async_handshake(ssl::stream_base::client,
        [self = shared_from_this()](const asio::error_code& ec) {
            if (ec || !self->active_) {
                std::cerr << "SSL Handshake failed: " << ec.message() << std::endl;
                return;
            }
            self->sendRangeRequest();
        });
}

void NetworkDownloader::sendHttpRequest() {
    LOG_INFO("Send HTTP request...");

    // 构造HTTP请求（需包含Host头）
    std::string request =
        "GET " + parsedUrl_.path + " HTTP/1.1\r\n"
        "Host: " + parsedUrl_.host + "\r\n"
        "Connection: close\r\n\r\n"; // 短连接

    // 异步发送请求
    asio::async_write(socket_, asio::buffer(request),
        [self = shared_from_this()](const asio::error_code& ec, size_t /*bytes*/) {
            if (ec || !self->active_) {
                std::cerr << "Send HTTP request failed: " << ec.message() << std::endl;
                self->shutdown();
                return;
            }
            // 请求发送成功后开始读取响应
            self->ParseHeaders();
        });
}

void NetworkDownloader::asyncReadChunks(std::ofstream& file) {
    //  一次可能会从 socket 里读出很多数据（比如 512 字节）所以有/r/n就行，后面可能还有很多数据
    asio::async_read_until(socket_, buffer_, "\r\n",
        [self = shared_from_this(), &file](const asio::error_code& ec, size_t bytes_transferred) {
            if (ec) {
                std::cerr << "Read error (chunk size line): " << ec.message() << std::endl;
                return;
            }

            // 从 buffer_ 中读取 chunk size 行
            std::istream chunk_stream(&self->buffer_);
            std::string chunk_size_str;

            //getline会从 buffer_ 里读取并消费数据，\n 被消费了 \r 被保留在 chunk_size_str 里
            std::getline(chunk_stream, chunk_size_str);

            if (!chunk_size_str.empty() && chunk_size_str.back() == '\r') {
                chunk_size_str.pop_back(); // 去掉 \r
            }

            DEBUG_COUT << "读取到的分块大小行: " << chunk_size_str << "\n";

            size_t chunk_size = 0;
            try {
                chunk_size = std::stoul(chunk_size_str, nullptr, 16);
            }
            catch (...) {
                DEBUG_CERR << "解析分块大小失败: " << chunk_size_str << "\n";
                return;
            }

            if (chunk_size == 0) {
                std::cout << "End of chunked transfer." << std::endl;
                self->shutdown();
                return;
            }

            // 读取 chunk body + \r\n 共 chunk_size + 2 字节
            self->asyncReadChunkData(chunk_size, file);
        });
}

void NetworkDownloader::asyncReadChunkData(size_t chunk_size, std::ofstream& file) {
    std::cout << "没读前buffer的大小 " << buffer_.size() << '\n';
    // 要求把 /r/n也读了，后面会丢弃
                            // 总共必须读取 n 字节（可能 buffer_ 里已存在）
    asio::async_read(socket_, buffer_, asio::transfer_exactly(chunk_size + 2),
        [self = shared_from_this(), chunk_size, &file](const asio::error_code& ec, size_t bytes_transferred) {

            if (ec && ec != asio::error::eof) {
                // 处理错误
                std::cerr << "Read error (chunk body): " << ec.message() << std::endl;
                return;
            }
            std::cout << "要求读取 " << chunk_size + 2 << " 字节的块数据 实际读取 " << bytes_transferred << " 字节）。\n";

            std::cout << "目前buffer的大小 " << self->buffer_.size() << '\n';

            std::istream chunk_stream(&self->buffer_);
            std::vector<char> chunk_data(chunk_size);
            chunk_stream.read(chunk_data.data(), chunk_size);

            // 写入文件
            file.write(chunk_data.data(), chunk_size);

            // 消费掉 chunk body + \r\n

            self->buffer_.consume(2);
            std::cout << "最后buffer的大小 " << self->buffer_.size() << '\n';

            if (ec == asio::error::eof) {
                std::cout << "正常结束（EOF），数据已全部读取。\n";
                //self->shutdown(); // 或其他结束动作
                return;
            }

            // 继续读取下一个 chunk
            self->asyncReadChunks(file);
        });
}
