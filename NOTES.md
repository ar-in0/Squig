Github: https://github.com/ar-in0/Squig

#### Resources
https://www.videosdk.live/blog/webrtc-vs-rtmp  
https://www.gumlet.com/learn/rtsp-vs-rtmp/  

RTMP spec (IMP): https://rtmp.veriskope.com/docs/spec/  

rtmp intro: https://rtmp.veriskope.com/docs/overview/ 


#### Notes
0. Stream from Iphone 13 to laptop running Linux 6.8  
-- Larix on Iphone to broadcast. (rtmp client)
-- Custom processing on laptop. (rtmp server)
See also: OBS Studio, OBS Camera for IOS (both open source)

![Packet capture from Larix to Squig Server](assets/rtmp-stream-dump.png)
- The unknowns have got to be video data. 
- There are some chunk IDs. See rtmp intro for meaning.
- Larix client strips the rtmp connection URL to the IP and port of the server
(which are set manually), and very likely does a simple TCP connect() to server IP:port, 
followed by the rtmp handshake with server


#### Misc
##### Endianness and Network Byte Order
- Recall endianness is a property of the ISA. Defined by how the CPU’s load/store instructions   
assemble or disassemble bytes when moving data between memory and registers.    
```
// Example: LOAD16 R0, [0x1000], i.e. pseudocode for
// loading 2 bytes at address 0x1000 into some register.

// In a little-endian ISA such as x86, the 
// actual instruction would be something like:
// store the 2-byte value at address 0x1000 in register AX
// i.e. mov eax [0x1000] 
// CPU implements this instruction via:
// ax = mem[0x1000] + (mem[0x1001] << 8) (0x0018 + 0x0022 << 8 = 0x2200)
//
// In the corresponding situation with a big-endian
// ISA, the CPU would perform:
// i.e. the value is as if the MSB is on the extreme left
// of the 2-byte sequence.
// R0 = (mem[0x1000] << 8) + mem[0x1001]
``` 
The network: Transmits bytes in some sequence.  
Network order: For multi-byte integers, the most significant BYTE (not bit) appears first in the stream.  
`htons`:  
```
uint16_t x = 0x1822;      // host-order value (may be used on a LE or BE ISA).
// On a little-endian system, htons() swaps the bytes of the value
// so that when the value is stored and transmitted, the most significant BYTE (not bit) is sent first.
// On little-endian: y == 0x2218
//
// On a big-endian system, htons() does nothing
// On big-endian: y == 0x1822
uint16_t y = htons(x);   // value transformed
```

#### Build
```
cmake -S /home/armaan/Fun-CS/Squig/submodules/EasyRTMP -B /home/armaan/Fun-CS/Squig/submodules/EasyRTMP/build
// add -DUSE_OPENSSL ON to enable openssl
// build as .ar is better, small size, good for embedded to (in case I build custom clients)

cmake --build /home/armaan/Fun-CS/Squig/submodules/EasyRTMP/build

doxygen Doxyfile : generates index.html for docs, open in browser.
```

@9 jan
https://doc.qt.io/qt-6/qvideoframe.html
Build a base gui to render the received video frames.

https://en.wikipedia.org/wiki/Real-Time_Messaging_Protocol
- Single view playback

Larix android video codec: AVC (H.264)
Larix IOS video codec: also AVC (H.264)

Q. Does EasyRTMP recreate frames? Answer NO.
RTMP messages contain encoded frames, need to decode.
- Does every RTMP message carry a single encoded frame in its payload? Likely NO.  
Actually, it seems like yes. A `VideoPacket` is an encoded frame. See rtmp_proto.h
Need to send videopackets to avc_decode.
- Actually, not quite. The videopacket data may contain several NAL units, i

Identifying Frames: https://stackoverflow.com/questions/3493742/problem-to-decode-h264-video-over-rtp-with-ffmpeg-libavcodec

https://stackoverflow.com/questions/58469017/libavcodec-initialization-to-achieve-real-time-playback-with-frame-dropping-when

https://github.com/leandromoreira/ffmpeg-libav-tutorial

https://archive.is/OrGfg : libavcodec audio decoding

https://www.ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3

Pipeline: Larix (RTMP packet) -> libav (decode AVC to get frames) -> ??QTMultimedia for playing frames?? 

https://stackoverflow.com/questions/6756770/libavcodec-how-to-tell-end-of-access-unit-when-decoding-h-264-stream
![alt text](assets/extracting-NALU.png)


How many NALU units in a frame?
https://stackoverflow.com/questions/28559529/how-to-know-the-number-of-nal-unit-in-h-264-stream-which-represent-a-picture

@14 Jan
Investigate: 
- In a 96 RTMP message capture, 140ish packets are sent to the interface.  
- Addtionally, the rtmp packets have different chunk stream ids in wireshark  
- How is wireshark timestamps calculated?
- In pause mode, packets are still being sent by larix. Timestamps are also incremented normally,
by ~33 units each RTMP message...


#### Github Stuff
Submodules workflow:
- Commit in submodule directory, then push
- git add deps/<name>
- Commit in root directory, then push


384 messages ~ 486-533 packets
96 messages ~

@15Jan
https://stackoverflow.com/questions/19191717/how-to-continuously-extract-video-frames-from-streaming-rtmp-using-avconv-ffmp
- Go through the ffmpeg source code. Apparently `ffmpeg -i <URL> -r 1 out%03d.jpg` generates jpg every second from RTMP stream.
- Need to create a VideoData object somehow.
FFMPeg API tutorial: http://www.dranger.com/ffmpeg/tutorial01.html

Extracting frames from an RTMP stream:  
There seem to be 3 methods,  
- [Ref](https://stackoverflow.com/questions/30096327/opencv-wont-capture-frames-from-a-rtmp-source-while-ffmpeg-does?rq=3) OpenCV can extract frames from an RTMP source url (uses ffmpeg). Is this URL of the Larix client on iOS or of my server on machine?
- FFMpeg CLI can generate .jpg every unit time given an rtmp URL. Again, which URL is this? I suspect server URL, but how would ffmpeg get the frames?
- The third method is using the base ffmpeg libraries in Squig for live frame extraction rather than depending on OpenCV.

Strategy: For any problem, before re-inventing any existing functionality, you must be able to demostrate 
that standard solutions are inadequate. In other words:  
Problem: Extracting frames from a live RTMP stream  
S0: Larix Client -> Nginx-rtmp -> VLC [ref1](https://github.com/aileone/nginx-rtmp-module/issues/1), [ref2](https://obsproject.com/forum/resources/how-to-set-up-your-own-private-rtmp-server-using-nginx.50/#:~:text=Restart%20nginx%20with:,web%20site%20you%20set%20up.)
- This is alright for playback/rtmp demo, but its difficult to integrate OpenCV/vision algorithms into nginx server...
S1: Using ffmpeg command line (Just to demonstrate, obviously not suited to realtime processing.)  
S2: Using OpenCV libraries (Might be viable)  
S3: Using libav* libraries directly in Squig (If OpenCV inadequate)  