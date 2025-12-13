#include "ImplAudioSource.h"
#include "utils/Logger.h"

LocalFileSource::LocalFileSource(const std::string& filePath)
    :filePath_(filePath){
    ma_result result = ma_decoder_init_file(filePath_.c_str(), nullptr, &decoder_);
    ma_decoder_get_length_in_pcm_frames(&decoder_, &totalFrames_);

    if (result == MA_SUCCESS)decoderInit_ = true;
    if (result != MA_SUCCESS) {
        LOG_ERROR("Decoder init failed! Path: %s, Result: %d", filePath_.c_str(), result);
        // 可以进一步抛出异常或标记资源无效
    }
}

LocalFileSource::~LocalFileSource() {
    // 这里构造的时候抛出错误，直接就构造失败，也不会调用析构函数了
    if (decoderInit_)
        ma_decoder_uninit(&decoder_);
}

ma_uint64 LocalFileSource::read(void* pOutput, const void* pInput, ma_uint32 frameCount) {
    //ma_uint64 cursor;
    //ma_decoder_get_cursor_in_pcm_frames(&decoder_, &cursor);
    //LOG_INFO("[Decoder] Cursor before read: %llu", cursor);

    ma_uint64 framesRead = 0;  
    ma_decoder_read_pcm_frames(
        &decoder_,      // 解码器实例
        pOutput,        // 输出缓冲区（存储解码后的PCM数据）
        frameCount,     // 请求读取帧数
        &framesRead);   // 实际读取帧数，返回的

    return framesRead;
}

ma_result LocalFileSource::seek(float percent) {
    ma_uint64 frameIndex = static_cast<ma_uint64>(percent * totalFrames_);

    ma_result result = ma_decoder_seek_to_pcm_frame(&decoder_, frameIndex);

    if (result != MA_SUCCESS) {
        LOG_WARN("Seek failed! Path: %s, Frame: %llu, Result: %d",
            filePath_.c_str(), frameIndex, result);
    }
    return result;
}

//-------------------------------------------------------------------------------------------------------\\

NetworkStreamSource::NetworkStreamSource(std::shared_ptr<NetworkDownloader> downloader) 
    : downloader_(std::move(downloader)) {

    //readProc回调函数，MiniAudio 会通过它读取音频数据
    auto readProc = [](ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, ma_uint64* pBytesRead) -> ma_result {
        auto* self = static_cast<NetworkStreamSource*>(pDecoder->pUserData);

        //// 这个和readBuffer里的cv看情况保留的
        //{
        //    std::unique_lock lock(self->downloader_->pauseMutex_);
        //    self->downloader_->pauseCV_.wait(lock, [&] { return !self->downloader_->isPaused_.load(); });  // 等待恢复
        //}

        // 实际的字节数，返回给上一层       从缓冲区读取,带wait的      // 期望的字节数
        *pBytesRead = self->downloader_->readBuffer(pBufferOut, bytesToRead);
        LOG_INFO("CallBack--readProc size: %llu", *pBytesRead);  // 输出读取的字节数

        if(pBytesRead==nullptr){
            LOG_INFO("pBytesRead nullptr!!!");
        }
        if (*pBytesRead == 0) {
            if (self->downloader_->isEndOfStream()) {
                LOG_ERROR("readProc：返回0字节，流结束了或网络错误");
                return MA_AT_END; // 或 MA_FAILED，看怎么处理
            }
            // 缓冲区还不够，但后续会有数据
            LOG_DEBUG("readProc：返回0字节，正在准备");
            return MA_BUSY;
        }
        return MA_SUCCESS;
        };

    //待改进，应该是根据数据源格式获得length长度然后range方式申请网络音频资源流
    auto seekProc = [](ma_decoder*, ma_int64, ma_seek_origin) -> ma_result {
        return MA_NOT_IMPLEMENTED;
        };

    // 初始化解码器（使用回调模式）
    ma_decoder_config config = ma_decoder_config_init_default();

    // 这个的话要在decoderinit之前调用，因为初始化的时候直接是要数据的
    downloader_->waitUntilBuffered(256 * 1024);

    auto funcptr = readProc;
    auto funcptr2 = seekProc;
    
    /*意思就是miniaudio的回调用是通过decoder来的，
   decoder存储了所有信息，我把我的this指针传到了decoder的pUserData里
   ，然后读取的时候调用readProc函数，这个函数调用了我的this里的逻辑*/
    ma_result result = ma_decoder_init(funcptr, funcptr2, this, &config, &decoder_);

    // 获取总字节 
    totalLengths_ = downloader_->totalLength();
    //ma_uint64 totalFrames_{};
    //ma_decoder_get_length_in_pcm_frames(&decoder_, &totalFrames_);

    if (result != MA_SUCCESS) {
        LOG_ERROR("解码器初始化失败: %s", ma_result_description(result));
    }
    decoderInit_ = true;
    //return MA_SUCCESS;
}

NetworkStreamSource::~NetworkStreamSource(){ 
    // 要考虑复用，不能直接释放downloader
    //NetworkDownloadMgr::getInstance().releaseDownloader(downloader_);
    
    // 这里构造的时候抛出错误，直接就构造失败，也不会调用析构函数了
    if (decoderInit_)
        ma_decoder_uninit(&decoder_);
}

ma_uint64 NetworkStreamSource::read(void* pOutput, const void* pInput, ma_uint32 frameCount) {

    // 这个是实际读的 frameCount是期待读的
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(
        &decoder_,      // 解码器实例
        pOutput,        // 输出缓冲区（存储解码后的PCM数据）
        frameCount,     // 请求读取帧数
        &framesRead);   // 实际读取帧数，返回的

    if(framesRead==0)
        LOG_INFO("Frame read: %llu", framesRead);

    return framesRead;
}

ma_result NetworkStreamSource::seek(float percent) {
    size_t pos = static_cast<size_t>(percent * totalLengths_);

    downloader_->seek(pos);

    return MA_SUCCESS;
}
