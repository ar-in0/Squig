#include <iostream>
#include <iomanip> // std::hex
#include <cctype> //std::isprint
#include <vector>
#include "squig/rtmp_server.h"
#include <stdint.h>
#include <opencv2/core/mat.hpp>

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
}

#include <deque>
namespace {
    // reference is always valid
    std::deque<librtmp::RTMPMediaMessage> rtmpFIFO {};
    int fifoIdx {};
    AVCodecContext* context = nullptr;
    librtmp::RTMPMediaMessage spsContainer;
}// namespace

void printHexDump(const std::vector<char>& buffer) {
    std::ios::fmtflags original_flags = std::cout.flags();
    char original_fill = std::cout.fill();

    const size_t bytesPerLine = 16;
    size_t length = buffer.size();

    for (size_t i = 0; i < length; i += bytesPerLine) {

        // 1. Print the Offset (e.g., 00000000)
        std::cout << std::hex << std::setw(8) << std::setfill('0') << i << ": ";

        // 2. Print the Hex Bytes
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < length) {
                // Cast to unsigned char first to avoid sign extension (e.g., ffffff80)
                // Then cast to int so streams treat it as a number, not a char
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
            } else {
                // Padding for the last line to align the ASCII column
                std::cout << "   ";
            }
        }

        std::cout << " |";

        // 3. Print the ASCII Representation
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < length) {
                char c = buffer[i + j];
                // Check if character is printable; otherwise replace with '.'
                std::cout << (std::isprint(static_cast<unsigned char>(c)) ? c : '.');
            }
        }
        std::cout << "|" << std::endl;
    }
    std::cout.flags(original_flags);
    std::cout.fill(original_fill);
}

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

    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    if (avcodec_open2(context, codec, NULL) < 0) {
    std::cout << "Failed to open avcontext and register codec info\n";
        exit(1);
    }
    // create the right codec
    // avcontext must have a codec registered in avctx -> codec
    // avcodec_open2(avcontext, avcodec, NULL)
    // open populates the pformatcontext object

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
void doDecodeLibAvc(const librtmp::RTMPMediaMessage& m) {
    // packet->data and packet->size need to be populated.
    uint8_t* pNaluData = reinterpret_cast<uint8_t*>(const_cast<char*>(m.video.video_data_send.data()));

    // pkt->size =  (unsigned char)pNaluData[0] << 24 | (unsigned char)pNaluData[1] << 16 | (unsigned char)pNaluData[2] << 8 | (unsigned char)pNaluData[3];
    //std::cout << "Size of NALU packet: " << pkt->size << std::endl;


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
    pkt->size = payloadSize;
    pkt->dts = m.timestamp;
    pkt->pts = pkt->dts + m.video.d.composition_time;
    pkt->data = pNaluData;

    int ret;
    ret = avcodec_send_packet(context, pkt); // Decode NAL
    if (ret < 0) {
        printf("error: %d\n", ret);
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    ret = avcodec_receive_frame(context, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return;
    else if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        exit(1);
    }
    printf("Frame decoded, dims: %d (width) x%d (height)\n", frame->width, frame->height);
    // generate cv::Mat

}

// First pass, play video on screen
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

    printHexDump(m.video.video_data_send);

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
                        printf("Videodata Frame Type (Keyframe, interframe): %d\n", message.video.d.frame_type);

                        spsContainer = message;
                        std::cout << std::endl;

                        printHexDump(message.video.video_data_send);

                        std::cout << std::endl;

                        initAvc(params);
                        fifoIdx++;

                        continue;

                    }

                    // Process rtmp message containing NALU
                    handleVideo(message);
                    fifoIdx++;
                    // Larix on ios streams at 30fps
                    // this is just exploratory
                    // if (rtmpFIFO.size() == 384) {
                    //    std::cout << "Got 384 RTMP Video messages, attempt to decode\n";
                    //    decodeRTMP(rtmpFIFO);
                    //    return 0;
                    // }
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
    }
}
