#include <iostream>
#include <vector>
#include "squig/rtmp_server.h"
#include "squig/utils.hpp"
#include <stdint.h>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
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

#include <deque>
namespace {
    // reference is always valid
    std::deque<librtmp::RTMPMediaMessage> rtmpFIFO {};
    int fifoIdx {};
    AVCodecContext* context = nullptr;
    AVFrame* pFrameYUV = nullptr;
    AVFrame* pFrameBGR = nullptr;
    librtmp::RTMPMediaMessage spsContainer;
    struct SwsContext* sws_ctx = nullptr;
}// namespace



void decodeMessage(const librtmp::RTMPMediaMessage& m);

void initAvc(librtmp::ClientParameters* params) {
    std::cout << "AVC Version: " << avcodec_version() << std::endl;
    av_log_set_level(AV_LOG_DEBUG);
    // client params:
    // to be passed to opencv later
    std::cout << "Client Stream Params:\n";
    std::cout << "URL: " << params->url << std::endl;
    std::cout << "Video Codec: " << (int)params->video_codec << std::endl;
    std::cout << "Video Dimensions: " << params->height << " x " << params->width << "\n\n";


    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    context = avcodec_alloc_context3(codec);
    if (!context) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    auto& extraData =  spsContainer.video.video_data_send;
    context->extradata_size = extraData.size();
    context->extradata = (uint8_t*)av_malloc(extraData.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(context->extradata, extraData.data(), extraData.size());
    // Need to add height, width info to context?
    // context->width = params->width;
    // context->height = params->height;
    context->pix_fmt = AV_PIX_FMT_YUV420P;
    context->width = params->width;
    context->height = params->height;
    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    if (avcodec_open2(context, codec, NULL) < 0) {
    std::cout << "Failed to open avcontext and register codec info\n";
        exit(1);
    }
    // create the right codec
    // avcontext must have a codec registered in avctx -> codec
    // avcodec_open2(avcontext, avcodec, NULL)
    // open populates the pformatcontext object
    pFrameYUV = av_frame_alloc();
    if (!pFrameYUV) {
        fprintf(stderr, "Could not allocate YUV video frame\n");
        exit(1);
    }

    pFrameBGR = av_frame_alloc();
    if (!pFrameBGR) {
        fprintf(stderr, "Could not allocate BGR video frame\n");
        exit(1);
    }
    // AV_PIX_FMT_YUV420P (i.e. = 0)
    printf("SRC pixel format: %d\n", context->pix_fmt);

    // get_buffer() needs frame->pixfmt, height, width to be set.
    // allocates heap memory for the decoded data
    // that (will eventually) be filled into
    // this AVFRame by sws_scale
    pFrameBGR->format = AV_PIX_FMT_BGR24;
    pFrameBGR->width = context->width;
    pFrameBGR->height = context->height;
   // allocate a buffer only once,
   // reuse for subsequent scaled frames
    int ret;
    ret = av_frame_get_buffer(pFrameBGR, 0);
    sws_ctx = sws_getContext(
        context->width,
        context->height,
        context->pix_fmt,
        context->width,
        context->height,
        AV_PIX_FMT_BGR24, // OpenCV uses BGR
        SWS_BICUBIC,
        NULL,
        NULL,
        NULL
    );
}

// RTMP->OpenCV Frames. Called on every incoming 
// rtmp packet.
void handleVideo(const librtmp::RTMPMediaMessage& m) {
    // do the decode -> Display
    // The first rtmp message (header, avc_packet_type=0) initializes the client params
    // i.e. video height frame rate etc. It is an AMF message. Confirmed
    // from printf in easyrtmp handleAMF()
    // RTMP -> AVPacket how?
    //

    decodeMessage(m);
}

// to decode NALUs into AVFrames.
// to decode NALUs into AVFrames.
void doDecodeLibAvc(const librtmp::RTMPMediaMessage& m) {
    // packet->data and packet->size need to be populated.
    uint8_t* pNaluData = reinterpret_cast<uint8_t*>(const_cast<char*>(m.video.video_data_send.data()));

    size_t payloadSize = m.video.video_data_send.size();

    // Convert the AVCC buffer to standard nalu bytestream format (needed by libav)
    size_t offset = 0;
    while (offset + 4 <= payloadSize) {
        // Read length of nalu
        uint32_t naluLen = (uint32_t)pNaluData[offset] << 24 |
            (uint32_t)pNaluData[offset + 1] << 16 |
            (uint32_t)pNaluData[offset + 2] << 8 |
            (uint32_t)pNaluData[offset + 3];

        // replace length bytes with nalu start code.
        pNaluData[offset]     = 0x00;
        pNaluData[offset + 1] = 0x00;
        pNaluData[offset + 2] = 0x00;
        pNaluData[offset + 3] = 0x01;

        // Jump to the next nalu length field
        offset += (4 + naluLen);
    }
    // send_packet requires start codes but
    // rtmp uses AVCC which is 4 byte length + raw data
    //  replace the length with a start code.

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate packet\n");
        return;
    }

    pkt->size = payloadSize;
    pkt->dts = m.timestamp;
    pkt->pts = pkt->dts + m.video.d.composition_time;
    pkt->data = pNaluData;

    int ret;
    ret = avcodec_send_packet(context, pkt); // Decode NAL

    // Packet is sent, we can free the wrapper now
    // (The data pointer refers to 'm', which is still valid)
    av_packet_free(&pkt);

    if (ret < 0) {
        printf("error: %d\n", ret);
        fprintf(stderr, "Error sending a packet for decoding\n");
        // exit(1); // Better to return than exit entire app
        return;
    }

    // receive_frame will allocate data buffer
    // in frameYUV to store decoded NALUs.
    ret = avcodec_receive_frame(context, pFrameYUV);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return;
    } else if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        exit(1);
    }

    printf("saving frame %3" PRId64 "\n", context->frame_num);
    printf("Dims: %d (width) x%d (height)\n",pFrameYUV->width, pFrameYUV->height);
    printf("frame->data[0]: %p, frame->linesize[0]: %d\n", (void*)pFrameYUV->data[0], pFrameYUV->linesize[0]);

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
    sws_scale(
        sws_ctx,
        (const uint8_t* const*)pFrameYUV->data,
        pFrameYUV->linesize,
        0,
        pFrameYUV->height,
        pFrameBGR->data,
        pFrameBGR->linesize
    );

    // frameBGR: converted BGR frame, frame: YUV src
    cv::Mat img(pFrameBGR->height, pFrameBGR->width, CV_8UC3, pFrameBGR->data[0], pFrameBGR->linesize[0]);

    cv::imshow("img", img);

    // 1ms delay needed to allow OpenCV to draw.
    // TODO move draw to an independent thread.
    cv::waitKey(1);

}

void decodeMessage(const librtmp::RTMPMediaMessage& m) {
    // From assts/extracting-NALU.png:
    // NALUs of the same frame have the same timestamp
    std::cout << "Decoding RTMP message " << fifoIdx << std::endl;
    std::cout << "Timestamp: " << m.timestamp << std::endl;
    std::cout << "Stream ID: " << m.message_stream_id << std::endl;
    // std::cout << "Video Data Type (1=NALU)" << (uint8_t)m.video.d.avc_packet_type << std::endl;
    // https://rtmp.veriskope.com/pdf/video_file_format_spec_v10.pdf
    // https://stackoverflow.com/questions/24884827/possible-locations-for-sequence-picture-parameter-sets-for-h-264-stream
    // video_data_send: One or more NALUs, in a buffer, either VCL or non-VCL
    // The bytes in the video_data_send specify the type of NALU, and then contain
    // the raw data.
    // VCL NALU: A slice of a video frame, contains pure image data. VCL type 5: NALU is
    // a single encoded image (video frame). VCL type 1:
    // Non-VCL NALU:
    printf("Video Frame Type (2: Interframe, 1: Keyframe,...): %d\n", m.video.d.frame_type);
    printf("Video Packet Type (0: Header for decoder, 1: Video Data): %d\n", m.video.d.avc_packet_type);
    std::cout << "Size of video payload: " << m.video.video_data_send.size() << std::endl;
    std::cout << std::endl;

    // utils::printHexDump(m.video.video_data_send);

    std::cout << std::endl;
    // RTMPMediaMessage -> AVPacket -> <avc_decode> -> AVFrame (uncompressed)
    // AVFrame.data -> cv::Mat() -> DISPLAY on screen!:
    // AVFrame: Uncompressed Video Frame
    // This func should output an AVFrame
    // for each RTMP Message.
    // https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/0_hello_world.c


    // int attribute_align_arg avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt)
    // AVPacket *av_packet_alloc(void)
    doDecodeLibAvc(m);
    // https://docs.opencv.org/3.4/d3/d63/classcv_1_1Mat.html#details
    // cvDecodeFrame()
}


int main() {
    std::cout <<"Sup bros" << std::endl;

    // start server
    TCPServer tcp_server(1935);
    auto client = tcp_server.accept();
    std::cout <<"conn accepted" << std::endl;

    librtmp::RTMPEndpoint rtmp_endpoint(client.get());
    librtmp::RTMPServerSession server_session(&rtmp_endpoint);

    // init avcodec

    try {
        while(true) {
            //receive media message
            //is an std::vector<char>
            librtmp::RTMPMediaMessage message = server_session.GetRTMPMessage();
            //   std::cout << "Got RTMP Message:\n";

            //auto message.video.video_data_send; // video payload corresp to 1 frame?
            //get received media codec parameters and streaming key
            auto params = server_session.GetClientParameters();
            switch (message.message_type){
                case librtmp::RTMPMessageType::VIDEO:
                    //  HandleVideo(message,params);
                    // std::cout << static_cast<int>(params->video_codec) << std::endl;
                    // std::cout << "[" << message.timestamp << "]" << "Video Message\n";
                    // rtmpFIFO.push_back(std::move(message));

                    // Initialize the decoder with first header
                    if (fifoIdx == 0) {
                        // Client always sends SPS, PPS in first rtmp message
                        // AVCC format.
                        spsContainer = message;
                        std::cout << std::endl;
                        // utils::printHexDump(message.video.video_data_send);
                        std::cout << std::endl;

                        initAvc(params);
                        fifoIdx++;
                        continue;
                    }

                    // Process rtmp message containing NALU
                    handleVideo(message);
                    fifoIdx++;
                    break;
                case librtmp::RTMPMessageType::AUDIO:
                    // std::cout << "[" << message.timestamp << "]" << "Audio Message\n";
                    break;
            }
        }
    } catch(...) {
        //connection terminated by peer or network conditions
        std::cout << "Connection Terminated\n";
        // decodeRTMP(rtmpFIFO);
        avcodec_free_context(&context);
        av_frame_unref (pFrameYUV);
        av_frame_free(&pFrameYUV);

        av_frame_unref (pFrameBGR);
        av_frame_free(&pFrameBGR);

        sws_freeContext(sws_ctx);
    }
}
