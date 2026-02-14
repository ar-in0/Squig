#ifndef STREAMDECODER_H
#define STREAMDECODER_H

#include <array>
#include <cstdint>
#include <memory>

// extern "C": FFmpeg is a C library. C++ mangles function names
// (e.g., av_frame_alloc becomes _Z14av_frame_allocv) so the linker
// can't find the .so symbols. extern "C" disables mangling for
// everything inside the block.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>

#include <libswscale/swscale.h>
}

#include <opencv2/core/mat.hpp>

#include "squig/framebuffer.h"
#include "squig/perfstatistics.hpp"
#include "squig/rtmp_server.h"

// RAII deleters for ffmpeg C resources.
// avcodec_free_context/av_frame_free take T** to null the caller's pointer,
// but here we pass a local copy's address—context is still freed.
struct AVFrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};

struct AVCodecCtxDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};

struct SwsCtxDeleter {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};

using UniqueAVFrame = std::unique_ptr<AVFrame, AVFrameDeleter>;
using UniqueAVCodecCtx = std::unique_ptr<AVCodecContext, AVCodecCtxDeleter>;
using UniqueSwsCtx = std::unique_ptr<SwsContext, SwsCtxDeleter>;

namespace Squig {

// Aggregate type -- all public members, no user constructors.
// Enables designated initializers: ImageFrame{.img = mat, .abort = true}
// Unnamed fields get value-initialized (.abort defaults to false).
struct ImageFrame {
    cv::Mat img;
    bool abort{false};
};

// allocated at init, reused.
struct PixFmtConversionPair {
    UniqueAVFrame pFrameYUV;  // in
    UniqueAVFrame pFrameBGR;  // out
};

}  // namespace Squig

// one per client, shared by network thread and decoder thread
class StreamDecoder {
   private:
    const librtmp::RTMPMediaMessage m_avccHdr;
    librtmp::ClientParameters m_sourceParams;

    const AVCodec* m_dec = nullptr;
    UniqueAVCodecCtx m_pDecCtx;
    UniqueSwsCtx m_pSwsCtx;

    // network thread writer, decoder thread reader
    FrameBuffer<librtmp::RTMPMediaMessage> m_rtmpFIFO{kRingBufferSize};

    // pre-allocated, overwritten every kRingBufferSize+1 frames.
    // Trails imFIFO reads by 1.
    // Invariant: T(imFIFO pop to cv::Mat processing) < T(RTMP message decode to
    // BGR)
    std::array<Squig::PixFmtConversionPair, kRingBufferSize + 1> m_avPool;

    // decoder thread writer, render thread reader
    FrameBuffer<Squig::ImageFrame> m_imFIFO{kRingBufferSize};

    PerfStatistics& m_stats;

    void initDecoder();
    void registerAVCCExtraData();
    void registerDecoderCtx();
    void registerPixelFmtConversionCtx();
    void h264AUDecodeToYUV(uint8_t* pAUData,
                           AVFrame* pDstFrameYUV,
                           size_t payloadSize,
                           uint64_t dTime,
                           uint32_t cTime);
    void naluAVCCToAnnexB(uint8_t* pNaluData, size_t payloadSize);
    void pixFmtYUVToBGR(AVFrame* pSrcFrameYUV, AVFrame* pDstFrameBGR);

   public:
    StreamDecoder(const librtmp::RTMPMediaMessage& m,
                  librtmp::ClientParameters& sourceParams,
                  PerfStatistics& stats);
    // = default: compiler generates the default destructor, which calls
    // each member's destructor. Since all resources are unique_ptr (RAII),
    // there's nothing manual to do. Writing = default (vs. omitting it)
    // documents that this was a deliberate choice, not an oversight.
    ~StreamDecoder() = default;

    void pushRTMP(librtmp::RTMPMediaMessage msg);
    void pushSentinel();
    void process();
    void renderPlayback();
};

#endif
