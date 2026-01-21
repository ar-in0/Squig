#include <iostream>
#include <vector>
#include "squig/rtmp_server.h"

// extern C is needed
// tells the compiler to
// not mangle symbols used in main.cpp
// that are present in avcodec.h
// This allows linker to find the correct (non-mangled)
// symbolname in libavcodec.so (libav is a C library)
extern "C" {
   #include <libavcodec/avcodec.h>
}

namespace {
   // reference is always valid
   std::vector<librtmp::RTMPMediaMessage> rtmpFIFO {};
} // namespace


void doDecodeLibAvc(const librtmp::RTMPMediaMessage& m) {
   std::cout << "AVC Version: " << avcodec_version() << std::endl;
}

// First pass, play video on screen
void decodeRTMP(const std::vector<librtmp::RTMPMediaMessage>& rtmpFIFO) {
   // From assts/extracting-NALU.png:
   // NALUs of the same frame have the same timestamp
   int cnt = 0;
   for (const auto& m: rtmpFIFO) {
      std::cout << "Decoding RTMP message " << cnt << std::endl;
      std::cout << "Timestamp: " << m.timestamp << std::endl;
      std::cout << "Stream ID: " << m.message_stream_id << std::endl;
      std::cout << "Video Data Type (1=NALU)" << m.video.d.avc_packet_type << std::endl;
      std::cout << "Size of video payload: " << m.video.video_data_send.size() << std::endl;
      std::cout << std::endl;
      cnt++;

      doDecodeLibAvc(m);
  }
}

int main() {
    std::cout <<"Sup bros" << std::endl;
    std::cout << "AVC Version: " << avcodec_version() << std::endl;

    // start server
    TCPServer tcp_server(1935);
    auto client = tcp_server.accept();
    std::cout <<"conn accepted" << std::endl;

    librtmp::RTMPEndpoint rtmp_endpoint(client.get());
    librtmp::RTMPServerSession server_session(&rtmp_endpoint);

    try{
        while(true){
           //receive media message
           auto message = server_session.GetRTMPMessage();
           std::cout << "Got RTMP Message:\n";

           //auto message.video.video_data_send; // video payload corresp to 1 frame?
           //get received media codec parameters and streaming key
           auto params = server_session.GetClientParameters();
           switch (message.message_type){			
              case librtmp::RTMPMessageType::VIDEO:
                //  HandleVideo(message,params);
                  // std::cout << static_cast<int>(params->video_codec) << std::endl;
                  // std::cout << "[" << message.timestamp << "]" << "Video Message\n";
                  rtmpFIFO.push_back(message);
                  // Larix on ios streams at 30fps
                  // this is just exploratory
                  if (rtmpFIFO.size() == 384) { // 4 seconds of video playback @ 24FPS, assuming 1 rtmp message has 1 frame.
                     std::cout << "Got 384 RTMP Video messages, attempt to decode\n";
                     decodeRTMP(rtmpFIFO);
                     return 0;
                  }
                  break;
              case librtmp::RTMPMessageType::AUDIO:
                  // std::cout << "[" << message.timestamp << "]" << "Audio Message\n";
                  break;
           }
        }
     }catch(...){
        //connection terminated by peer or network conditions
        std::cout << "Connection Terminated\n";
     }
}
