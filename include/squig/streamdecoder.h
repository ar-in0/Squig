#ifndef STREAMDECODER_H
#define STREAMDECODER_H

#include <iostream>
#include <stdint.h>

// extern C is needed
// tells the compiler to
// not mangle symbols used in main.cpp
// that are present in avcodec.h
// This allows linker to find the correct (non-mangled)
// symbolname in libavcodec.so (libav is a C library)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include "squig/perfstatistics.hpp"
#include "squig/rtmp_server.h"

// A StreamDecoder is responsible
// for transforming encoded YUV NALUs from RTMP messages
// to decoded BGR Video Frames, usable by openCV.
// The StreamDecoder makes a buffer of AVFrames
// from a single client available for playback or analysis.
class StreamDecoder {
private:
    int m_fifoIdx {};
    // RTMP message containing AVCDecoderConfigurationRecord
    // i.e. AVCC header i.e. first RTMP message.
    const librtmp::RTMPMediaMessage m_avccHdr;

    // non-const for later multi-resoultion source
    // support...
    librtmp::ClientParameters m_sourceParams;

    // H.264 decoder context
    const AVCodec* m_dec = nullptr;
    AVCodecContext* m_pDecCtx = nullptr;

    // pixel format conversion context
    struct SwsContext* m_pSwsCtx = nullptr;

    // YUV: transport pixel format, compact
    // BGR: Required by openCV render methods
    // Frames are allocated once, and buffers
    // are reused until session ends.
    AVFrame* m_pFrameYUV, *m_pFrameBGR;

    PerfStatistics& m_stats;
    // AU = Access Unit (= Video Frame thanks to easyRTMP)
    void initDecoder();
    void registerAVCCExtraData();
    void registerDecoderCtx();
    void registerPixelFmtConversionCtx();
    void h264AUDecode(uint8_t* pAUData, size_t payloadSize, uint64_t dTime, uint32_t cTime);
    void naluAVCCToAnnexB(uint8_t* pNaluData, size_t payloadSize);
    void pixFmtYUVToBGR();
    void updateImshowTime(uint64_t now);
public:
    StreamDecoder(const librtmp::RTMPMediaMessage& m, librtmp::ClientParameters& sourceParams, PerfStatistics& stats);
    void process(librtmp::RTMPMediaMessage& m);
    ~StreamDecoder();
};
#endif
