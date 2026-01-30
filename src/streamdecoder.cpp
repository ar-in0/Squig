#include "squig/streamdecoder.h"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include "squig/utils.hpp"

// 1. Is m_avccHdr doing a const to non-const conversion?
StreamDecoder::StreamDecoder(const librtmp::RTMPMediaMessage& m,
                             librtmp::ClientParameters& sourceParams,
                             PerfStatistics& stats)
    : m_avccHdr(m), m_sourceParams(sourceParams), m_stats(stats) {
    //  get AV_CODEC ID from params->video_codec
    // codec_id.h
    AVCodecID cID = AV_CODEC_ID_H264;
    m_dec = avcodec_find_decoder(cID);

    m_pDecCtx = avcodec_alloc_context3(m_dec);

    initDecoder();
}

void StreamDecoder::initDecoder() {
    registerAVCCExtraData();
    registerDecoderCtx();
    registerPixelFmtConversionCtx();
}

void StreamDecoder::registerDecoderCtx() {
    // Ensure decoder knows video resolution
    m_pDecCtx->width = m_sourceParams.width;
    m_pDecCtx->height = m_sourceParams.height;
    m_pDecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    avcodec_open2(m_pDecCtx, m_dec, NULL);
}

void StreamDecoder::registerAVCCExtraData() {
    auto& eData = m_avccHdr.video.video_data_send;
    m_pDecCtx->extradata =
        (uint8_t*)av_mallocz(eData.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    m_pDecCtx->extradata_size = eData.size();

    // copy the extradata to the decoder context.
    memcpy(m_pDecCtx->extradata, eData.data(), eData.size());
}

void StreamDecoder::registerPixelFmtConversionCtx() {
    // Allocate frame once and keep
    // overwiting framedata. Saves
    // alloc on each RTMP message.
    // May change with buffering scheme.
    m_pFrameYUV = av_frame_alloc();
    m_pFrameBGR = av_frame_alloc();

    // get_buffer() needs frame->pixfmt, height, width to be set.
    // allocates heap memory for the decoded data
    // that (will eventually) be filled into
    // this AVFRame by sws_scale
    m_pFrameBGR->format = AV_PIX_FMT_BGR24;
    m_pFrameBGR->width = m_pDecCtx->width;
    m_pFrameBGR->height = m_pDecCtx->height;

    // allocate a buffer only once,
    // reuse for subsequent scaled frames
    int ret;
    ret = av_frame_get_buffer(m_pFrameBGR, 0);

    // input and output frame resolutions must be the same.
    m_pSwsCtx = sws_getContext(m_pDecCtx->width,
                               m_pDecCtx->height,
                               m_pDecCtx->pix_fmt,
                               m_pDecCtx->width,
                               m_pDecCtx->height,
                               AV_PIX_FMT_BGR24,  // OpenCV uses BGR
                               SWS_BICUBIC,
                               NULL,
                               NULL,
                               NULL);
}

// Convert the AVCC buffer to standard nalu bytestream format (needed by libav)
// send_packet requires start codes but
// rtmp uses AVCC which is 4 byte length + raw data
//  replace the length with a start code.
void StreamDecoder::naluAVCCToAnnexB(uint8_t* pNaluData, size_t payloadSize) {
    size_t offset = 0;
    while (offset + 4 <= payloadSize) {
        // Read length of nalu
        uint32_t naluLen = (uint32_t)pNaluData[offset] << 24 |
                           (uint32_t)pNaluData[offset + 1] << 16 |
                           (uint32_t)pNaluData[offset + 2] << 8 |
                           (uint32_t)pNaluData[offset + 3];

        // replace length bytes with nalu start code.
        pNaluData[offset] = 0x00;
        pNaluData[offset + 1] = 0x00;
        pNaluData[offset + 2] = 0x00;
        pNaluData[offset + 3] = 0x01;

        // Jump to the next nalu length field
        offset += (4 + naluLen);
    }
}

void StreamDecoder::h264AUDecode(uint8_t* pAUData,
                                 size_t payloadSize,
                                 uint64_t dTime,
                                 uint32_t cTime) {
    // packet->data and packet->size need to be populated.
    // One AVPacket per RTMP Access Unit (i.e. Video Frame)
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate packet\n");
        return;
    }

    pkt->size = payloadSize;
    pkt->dts = dTime;
    pkt->pts = pkt->dts + cTime;
    pkt->data = pAUData;

    int ret;
    ret = avcodec_send_packet(m_pDecCtx, pkt);  // Decode NAL

    // Packet is sent, we can free the wrapper now
    // (The data pointer refers to 'm', which is still valid)
    av_packet_free(&pkt);

    if (ret < 0) {
        printf("error: %d\n", ret);
        fprintf(stderr, "Error sending a packet for decoding\n");
        return;
    }

    // receive_frame will allocate data buffer
    // in frameYUV to store decoded NALUs.
    ret = avcodec_receive_frame(m_pDecCtx, m_pFrameYUV);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return;
    } else if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        exit(1);
    }
}

void StreamDecoder::pixFmtYUVToBGR() {
    sws_scale(m_pSwsCtx,
              (const uint8_t* const*)m_pFrameYUV->data,
              m_pFrameYUV->linesize,
              0,
              m_pFrameYUV->height,
              m_pFrameBGR->data,
              m_pFrameBGR->linesize);
}

// pass by non-const reference need to modify contents
// of m... in nalutoAnnexB
// ref is valid until lifetime of object
void StreamDecoder::process(librtmp::RTMPMediaMessage& m) {
    // RTMPMediaMessage -> AVPacket -> <avc_decode> -> AVFrame (uncompressed)
    // AVFrame.data -> cv::Mat() -> DISPLAY on screen!:
    // AVFrame: Uncompressed Video Frame
    // This func should output an AVFrame
    // for each RTMP Message.
    // https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/0_hello_world.c

    // convert video payload to AnnexB format for ffmpeg
    // Casting ensure safety. std::vector<T>, .data() returns T*
    // Here, we convert the returned char* to const char*, and then
    // reinterpret.
    uint8_t* pNaluData = reinterpret_cast<uint8_t*>(
        const_cast<char*>(m.video.video_data_send.data()));
    size_t payloadSize = m.video.video_data_send.size();
    naluAVCCToAnnexB(pNaluData, payloadSize);

    uint32_t cTime = m.video.d.composition_time;
    uint64_t dTime = m.timestamp;
    h264AUDecode(pNaluData, payloadSize, dTime, cTime);

    // release any data previouslywritten to the
    // undo any previous get_buffer().
    // av_frame_unref(pFrameBGR);

    // To avoid alloc/free buffer on each rtmp packet decode,
    // reuse the original buffer, i.e. let
    // sws_scale overwrite a previously filled buffer.
    // Possible future issue: If resolution of
    // incoming stream changes in a session,
    // there could be trouble. (cv::mat reads part-old data)
    // --
    // In such a case, can add a check here to verify no change
    // in codec context, but rn the main loop logic itself only
    // allows a single resolution per session.
    // (fifoIdx=0 is used to get the SPS, and initAvc()).

    // write to pFrameBGR data buffer
    // OpenCV methods only work with BGR frames,
    // but video is transmitted as YUV.
    pixFmtYUVToBGR();

    // TODO Add YUV Frame to a shared AVFrame Buffer
    // for playback/analysis.
    //
    // Current: Display frame immediately
    cv::Mat img(m_pFrameBGR->height,
                m_pFrameBGR->width,
                CV_8UC3,
                m_pFrameBGR->data[0],
                m_pFrameBGR->linesize[0]);

    // get curr time
    // update currtime
    updateImshowTime(utils::nowMs());

            auto start = std::chrono::high_resolution_clock::now();
cv::imshow("Video Playback", img);
                    auto end = std::chrono::high_resolution_clock::now();
                    uint64_t durationUs =
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            end - start)
                        .count();
m_stats.update(durationUs);
    // 1ms delay needed to allow OpenCV to draw.
    // TODO move draw to an independent thread.
    cv::waitKey(1);
}

void StreamDecoder::updateImshowTime(uint64_t now) {
    m_stats.updateImshowTime(now);
}

StreamDecoder::~StreamDecoder() {
    if (m_pDecCtx) {
        avcodec_free_context(&m_pDecCtx);
    }
    if (m_pFrameYUV) {
        av_frame_free(&m_pFrameYUV);
    }
    if (m_pFrameBGR) {
        av_frame_free(&m_pFrameBGR);
    }
    if (m_pSwsCtx) {
        sws_freeContext(m_pSwsCtx);
    }
}
