#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>

#include "rtmp_endpoint.h"
#include "squig/perfstatistics.hpp"
#include "squig/streamdecoder.h"
#include "squig/utils.hpp"

namespace {
int fifoIdx{};
std::unique_ptr<StreamDecoder> sd;
}  // namespace

PerfStatistics stats(utils::nowMs());

void handleVideo(librtmp::RTMPMediaMessage m,
                 librtmp::ClientParameters* sourceParams) {
    // do the Decode -> Display
    // The first rtmp message (header, avc_packet_type=0) initializes the client
    // params i.e. video height frame rate etc. It is an AMF message. Confirmed
    // from printf in easyrtmp handleAMF()

    // From assts/extracting-NALU.png:
    // NALUs of the same frame have the same timestamp
    // ---
    // https://rtmp.veriskope.com/pdf/video_file_format_spec_v10.pdf
    // https://stackoverflow.com/questions/24884827/possible-locations-for-sequence-picture-parameter-sets-for-h-264-stream
    // video_data_send: One or more NALUs, in a buffer, either VCL or non-VCL
    // The bytes in the video_data_send are in AVCC format,
    // they specify the length of NALU, and then contain the raw data.
    // EasyRTMP populates m.d with supplementary RTMP info as in spec.
    // EasyRTMP ensures each MediaMessage contains a single Access Unit (Frame)
    // which is made up of several NALUs.
    //
    // VCL NALU: A slice of a video frame, contains pure image data.
    // VCL type 5: NALU is a single encoded image (video frame).
    // VCL type 1: Non-VCL NALU:
    const char* fType = (m.video.d.frame_type == 1)
                            ? "Keyframe"
                            : "Interframe";  // 2 = interframe
    const char* pType =
        (m.video.d.avc_packet_type == 0) ? "AVCC-Hdr" : "AVCC-Access-Unit";  //

    // Single line output with fixed spacing
    // printf("[RTMP %-3d] TS:%-8ld SID:%-3d | %-5s | %-8s | Size:%4zu\n",
    //        fifoIdx,
    //        m.timestamp,
    //        m.message_stream_id,
    //        fType,
    //        pType,
    //        m.video.video_data_send.size());

    // utils::printHexDump(m.video.video_data_send);

    bool isAVCCHdr = (m.video.d.avc_packet_type == 0);
    if (isAVCCHdr) {
        sd = std::make_unique<StreamDecoder>(m, *sourceParams, stats);
        return;
    }

    // add to sd queue
    sd.rtmpFifo.push(m);
}

// decoder thread. Write to
// imgFifo
void decodeRTMP() {
    // RTMPMediaMessage -> AVPacket -> <avc_decode> -> AVFrame
    // (uncompressed) AVFrame.data -> cv::Mat() -> DISPLAY on screen!:
    // AVFrame: Uncompressed Video Frame
    // This func should output an AVFrame
    // for each RTMP Message.
    // https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/0_hello_world.c
    // This should be in the decoder thread
    if (sd) {
        sd->process();
    }
}

void networkRecv() {
    TCPServer tcp_server(1935);
    // client = accept socket
    auto client = tcp_server.accept();
    std::cout << "conn accepted" << std::endl;

    librtmp::RTMPEndpoint rtmp_endpoint(client.get());
    librtmp::RTMPServerSession server_session(&rtmp_endpoint);
    try {
        for (;;) {
            auto start = std::chrono::high_resolution_clock::now();
            librtmp::RTMPMediaMessage message = server_session.GetRTMPMessage();

            // get received media codec parameters and streaming key
            auto params = server_session.GetClientParameters();
            switch (message.message_type) {
                case librtmp::RTMPMessageType::VIDEO: {
                    // write to rtmpFifo.
                    handleVideo(message, params);
                    // auto end = std::chrono::high_resolution_clock::now();
                    // uint64_t durationUs =
                    //     std::chrono::duration_cast<std::chrono::microseconds>(
                    //         end - start)
                    //         .count();
                    // stats.update(durationUs);
                    fifoIdx++;
                    break;
                }  // braces needed if declaring vars inside a case
                case librtmp::RTMPMessageType::AUDIO:
                    // std::cout << "[" << message.timestamp << "]" << "Audio
                    // Message\n";
                    break;
            }
        }
    } catch (...) {
        std::cout << "Connection Terminated\n";
        std::cout << "Min Time: " << stats.min() << "\n";
        std::cout << "Max Time: " << stats.max() << "\n";
        // std::cout << "p99E2E Time: " << stats.p99E2E() << "\n";
        std::cout << "p99Imshow Period: " << stats.p99Imshow() << "\n";

        // write sentinel to rtmpFIFO
        librtmp::RTMPMediaMessage sentinel{};
        sentinel.message_type = librtmp::RTMPMessageType::ABORT;
        sd->rtmpFIFO.push(sentinel);
        return;
    }
}

int main() {
    std::cout << "Sup Bros\n";
    // Thread A: network thread, recv() and write to RTMPFifo
    // Thread B: Decode thread, read rtmpFifo and write imgFifo
    // Main thread: Reads from imgFifo and does imshow()
    // every thread has access to the streamdecoder unique_ptr.

    std::thread nw(networkRecv);
    std::thread dc(decodeRTMP);

    sd->renderPlayback();

    nw.join();
    dc.join();
}
