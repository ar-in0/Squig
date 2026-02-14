#include "squig/streamdecoder.h"

#include <cstdio>
#include <cstring>

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include "squig/utils.hpp"

// Member initializer list: members are initialized BEFORE the body runs.
// Required for const members (m_avccHdr) and references (m_stats) -- you
// can't assign to const/ref in the body. For everything else, it's still
// preferred: direct construction vs default-construct-then-assign.
StreamDecoder::StreamDecoder(const librtmp::RTMPMediaMessage& m,
                             librtmp::ClientParameters& sourceParams,
                             PerfStatistics& stats)
    : m_avccHdr(m), m_sourceParams(sourceParams), m_stats(stats) {
    m_dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    m_pDecCtx.reset(avcodec_alloc_context3(m_dec));
    initDecoder();
}

void StreamDecoder::initDecoder() {
    registerAVCCExtraData();
    registerDecoderCtx();
    registerPixelFmtConversionCtx();
}

void StreamDecoder::registerDecoderCtx() {
    m_pDecCtx->width = m_sourceParams.width;
    m_pDecCtx->height = m_sourceParams.height;
    m_pDecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    avcodec_open2(m_pDecCtx.get(), m_dec, nullptr);
}

void StreamDecoder::registerAVCCExtraData() {
    auto& eData = m_avccHdr.video.video_data_send;
    m_pDecCtx->extradata = static_cast<uint8_t*>(
        av_mallocz(eData.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    m_pDecCtx->extradata_size = static_cast<int>(eData.size());

    std::memcpy(m_pDecCtx->extradata, eData.data(), eData.size());
}

void StreamDecoder::registerPixelFmtConversionCtx() {
    for (auto& [yuv, bgr] : m_avPool) {
        yuv.reset(av_frame_alloc());
        bgr.reset(av_frame_alloc());

        bgr->format = AV_PIX_FMT_BGR24;
        bgr->width = m_pDecCtx->width;
        bgr->height = m_pDecCtx->height;

        av_frame_get_buffer(bgr.get(), 0);
    }

    m_pSwsCtx.reset(sws_getContext(m_pDecCtx->width,
                                   m_pDecCtx->height,
                                   m_pDecCtx->pix_fmt,
                                   m_pDecCtx->width,
                                   m_pDecCtx->height,
                                   AV_PIX_FMT_BGR24,
                                   SWS_BICUBIC,
                                   nullptr,
                                   nullptr,
                                   nullptr));
}

// replace AVCC length prefixes with Annex-B start codes
// send_packet requires start codes but
// rtmp uses AVCC which is 4 byte length + raw data
void StreamDecoder::naluAVCCToAnnexB(uint8_t* pNaluData, size_t payloadSize) {
    size_t offset = 0;
    while (offset + 4 <= payloadSize) {
        uint32_t naluLen = static_cast<uint32_t>(pNaluData[offset]) << 24 |
                           static_cast<uint32_t>(pNaluData[offset + 1]) << 16 |
                           static_cast<uint32_t>(pNaluData[offset + 2]) << 8 |
                           static_cast<uint32_t>(pNaluData[offset + 3]);

        pNaluData[offset] = 0x00;
        pNaluData[offset + 1] = 0x00;
        pNaluData[offset + 2] = 0x00;
        pNaluData[offset + 3] = 0x01;

        offset += 4 + naluLen;
    }
}

void StreamDecoder::h264AUDecodeToYUV(uint8_t* pAUDataSrc,
                                      AVFrame* pFrameYUVDst,
                                      size_t payloadSize,
                                      uint64_t dTime,
                                      uint32_t cTime) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        std::fprintf(stderr, "Could not allocate packet\n");
        return;
    }

    pkt->size = static_cast<int>(payloadSize);
    pkt->dts = static_cast<int64_t>(dTime);
    pkt->pts = pkt->dts + cTime;
    pkt->data = pAUDataSrc;

    int ret = avcodec_send_packet(m_pDecCtx.get(), pkt);

    // pkt is a wrapper; pkt->data points into m's buffer (still valid).
    av_packet_free(&pkt);

    if (ret < 0) {
        std::fprintf(stderr, "Error sending packet for decoding: %d\n", ret);
        return;
    }

    ret = avcodec_receive_frame(m_pDecCtx.get(), pFrameYUVDst);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return;
    } else if (ret < 0) {
        std::fprintf(stderr, "Error during decoding: %d\n", ret);
    }
}

void StreamDecoder::pixFmtYUVToBGR(AVFrame* pSrcFrameYUV,
                                    AVFrame* pDstFrameBGR) {
    sws_scale(m_pSwsCtx.get(),
              pSrcFrameYUV->data,
              pSrcFrameYUV->linesize,
              0,
              pSrcFrameYUV->height,
              pDstFrameBGR->data,
              pDstFrameBGR->linesize);
}

void StreamDecoder::pushRTMP(librtmp::RTMPMediaMessage msg) {
    m_rtmpFIFO.push(std::move(msg));
}

void StreamDecoder::pushSentinel() {
    librtmp::RTMPMediaMessage sentinel{};
    sentinel.message_type = librtmp::RTMPMessageType::ABORT;
    m_rtmpFIFO.push(std::move(sentinel));
}

void StreamDecoder::process() {
    size_t poolIdx = 0;
    for (;;) {
        auto m = m_rtmpFIFO.pop();

        if (m.message_type == librtmp::RTMPMessageType::ABORT) {
            m_imFIFO.push(Squig::ImageFrame{.abort = true});
            return;
        }

        // m is a local value (popped by move), safe to modify in-place
        // reinterpret_cast: type-pun char* -> uint8_t*. Same size, same representation,
        // just a different type for FFmpeg's API. The old code used const_cast to strip
        // const (undefined behavior if the data was actually const). Here the data is
        // non-const (we own it via pop-by-move), so reinterpret_cast is correct.
        auto* pNaluData =
            reinterpret_cast<uint8_t*>(m.video.video_data_send.data());
        size_t payloadSize = m.video.video_data_send.size();
        naluAVCCToAnnexB(pNaluData, payloadSize);

        auto* pFrameYUV = m_avPool[poolIdx].pFrameYUV.get();
        auto* pFrameBGR = m_avPool[poolIdx].pFrameBGR.get();

        h264AUDecodeToYUV(pNaluData, pFrameYUV, payloadSize, m.timestamp,
                          m.video.d.composition_time);
        pixFmtYUVToBGR(pFrameYUV, pFrameBGR);

        // cv::Mat header wraps pFrameBGR->data (no copy).
        // safe as long as render consumes before pool index wraps.
        cv::Mat img(pFrameBGR->height, pFrameBGR->width, CV_8UC3,
                    pFrameBGR->data[0], pFrameBGR->linesize[0]);

        m_imFIFO.push(Squig::ImageFrame{.img = img});
        poolIdx = (poolIdx + 1) % m_avPool.size();
    }
}

void StreamDecoder::renderPlayback() {
    for (;;) {
        auto im = m_imFIFO.pop();
        if (im.abort) return;

        m_stats.updateImshowTime(utils::nowMs());
        cv::imshow("Video Playback", im.img);
        cv::waitKey(1);
    }
}
