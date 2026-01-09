#include <iostream>
#include "squig/rtmp_server.h"

int main() {
    std::cout <<"Sup bros" << std::endl;

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
           //auto message.video.video_data_send; // video payload corresp to 1 frame?
           //get received media codec parameters and streaming key
           auto params = server_session.GetClientParameters();
           switch (message.message_type){			
              case librtmp::RTMPMessageType::VIDEO:
                //  HandleVideo(message,params);
                  std::cout << static_cast<int>(params->video_codec) << std::endl;
                  std::cout << "[" << message.timestamp << "]" << "Video Message\n";
                  break;
              case librtmp::RTMPMessageType::AUDIO:
                  std::cout << "[" << message.timestamp << "]" << "Audio Message\n";
                  break;
           }
        }
     }catch(...){
        //connection terminated by peer or network conditions
        std::cout << "Connection Terminated\n";
     }
}   