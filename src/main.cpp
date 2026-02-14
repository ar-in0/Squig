#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include "rtmp_endpoint.h"
#include "squig/perfstatistics.hpp"
#include "squig/streamdecoder.h"
#include "squig/utils.hpp"

// anonymous namespace: gives everything inside internal linkage (file-local).
// Same effect as 'static' on each declaration, but works for types too.
// Without this, sdMutex/sdReady/sd would be visible to other translation
// units and could cause ODR (One Definition Rule) violations.
namespace {
std::mutex sdMutex;
std::condition_variable sdReady;
std::unique_ptr<StreamDecoder> sd;
PerfStatistics stats(utils::nowMs());
}  // namespace

// network thread
void nwHandleVideo(librtmp::RTMPMediaMessage m,
                   librtmp::ClientParameters* sourceParams) {
    // The first rtmp message (header, avc_packet_type=0) initializes the client
    // params i.e. video height frame rate etc. It is an AMF message. Confirmed
    // from printf in easyrtmp handleAMF()
    //
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

    bool isAVCCHdr = (m.video.d.avc_packet_type == 0);
    if (isAVCCHdr) {
        {
            std::lock_guard lock(sdMutex);
            // make_unique: heap-allocates and constructs in one call. No raw 'new'.
            // Exception-safe: if the constructor throws, no memory leaks.
            // Returns unique_ptr -- sd owns the StreamDecoder, auto-deletes on scope exit.
            sd = std::make_unique<StreamDecoder>(m, *sourceParams, stats);
        }
        // notify_all: both decoder and main thread wait on this
        sdReady.notify_all();
        return;
    }

    sd->pushRTMP(std::move(m));
}

// decoder thread
void decodeRTMP() {
    {
        // Lambda predicate: [] captures nothing (sd is in the anonymous namespace,
        // accessible without capture). The CV re-checks this after every spurious
        // wake or notify -- the predicate guards against both.
        std::unique_lock lock(sdMutex);
        sdReady.wait(lock, [] { return sd != nullptr; });
    }
    sd->process();
}

void networkRecv() {
    TCPServer tcp_server(1935);
    auto client = tcp_server.accept();
    std::cout << "conn accepted\n";

    librtmp::RTMPEndpoint rtmp_endpoint(client.get());
    librtmp::RTMPServerSession server_session(&rtmp_endpoint);
    try {
        for (;;) {
            librtmp::RTMPMediaMessage message =
                server_session.GetRTMPMessage();
            auto params = server_session.GetClientParameters();
            switch (message.message_type) {
                case librtmp::RTMPMessageType::VIDEO:
                    nwHandleVideo(std::move(message), params);
                    break;
                case librtmp::RTMPMessageType::AUDIO:
                    break;
            }
        }
    } catch (...) {
        std::cout << "Connection Terminated\n";
        sd->pushSentinel();
    }
}

int main() {
    // Thread A: network thread, recv() and write to RTMPFifo
    // Thread B: Decode thread, read rtmpFifo and write imgFifo
    // Main thread: Reads from imgFifo and does imshow()
    // jthread constructor spawns the thread immediately.
    // Unlike std::thread, jthread auto-joins in its destructor --
    // if renderPlayback() throws, stack unwinding joins both threads cleanly.
    std::jthread nw(networkRecv);
    std::jthread dc(decodeRTMP);

    // main thread must also wait for sd init before calling renderPlayback
    {
        std::unique_lock lock(sdMutex);
        sdReady.wait(lock, [] { return sd != nullptr; });
    }

    sd->renderPlayback();

    // stats access is safe here: render loop exited means
    // sentinel was received, so no concurrent writers
    std::cout << "p99Imshow Period: " << stats.p99Imshow() << "\n";
}
