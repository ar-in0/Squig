#include <iostream>
#include <vector>
#include "squig/rtmp_server.h"
#include <stdint.h>

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
   int fifoIdx {};
} // namespace

void decodeRTMPMsg(const librtmp::RTMPMediaMessage& m);

void initAvc() {
   ;
}

// RTMP->OpenCV Frames. Called on every incoming 
// rtmp packet.
void handleVideo() {
   // do the decode -> Display
   auto &m = rtmpFIFO[fifoIdx];
   // The first rtmp message (header, avc_packet_type=0) initializes the client params
   // i.e. video height frame rate etc. It is an AMF message. Confirmed
   // from printf in easyrtmp handleAMF()
   decodeRTMPMsg(m);
   fifoIdx++;
}


void doDecodeLibAvc(const librtmp::RTMPMediaMessage& m) {
   std::cout << "AVC Version: " << avcodec_version() << std::endl;

}

// First pass, play video on screen
void decodeRTMPMsg(const librtmp::RTMPMediaMessage& m) {
   // From assts/extracting-NALU.png:
   // NALUs of the same frame have the same timestamp
   std::cout << "Decoding RTMP message " << fifoIdx << std::endl;
   std::cout << "Timestamp: " << m.timestamp << std::endl;
   std::cout << "Stream ID: " << m.message_stream_id << std::endl;
   // std::cout << "Video Data Type (1=NALU)" << (uint8_t)m.video.d.avc_packet_type << std::endl;
   printf("Video Packet Type (0: Header for decoder, 1: Video Data): %d\n", m.video.d.avc_packet_type);
   std::cout << "Size of video payload: " << m.video.video_data_send.size() << std::endl;
   std::cout << std::endl;

   // AVFrame: Uncompressed Video Frame
   // This func should output an AVFrame
   // for each RTMP Message.
   // https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/0_hello_world.c
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
         auto message = server_session.GetRTMPMessage();
      //   std::cout << "Got RTMP Message:\n";

         //auto message.video.video_data_send; // video payload corresp to 1 frame?
         //get received media codec parameters and streaming key
         auto params = server_session.GetClientParameters();
         if (!fifoIdx) {
            initAvc();
            // client params: 
            // to be passed to opencv later
            std::cout << "Client Stream Params:\n";
            std::cout << "URL: " << params->url << std::endl;
            std::cout << "Video Codec: " << (int)params->video_codec << std::endl;
            std::cout << "Video Dimensions: " << params->height << " x " << params->width << "\n\n";
         }

         switch (message.message_type){			
            case librtmp::RTMPMessageType::VIDEO:
               //  HandleVideo(message,params);
               // std::cout << static_cast<int>(params->video_codec) << std::endl;
               // std::cout << "[" << message.timestamp << "]" << "Video Message\n";
               rtmpFIFO.push_back(message);
               handleVideo();
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
