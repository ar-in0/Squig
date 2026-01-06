# rtmp-cpp

A lightweight C++ RTMP server library with C-compatible API.

## Features
- Full RTMP protocol implementation (handshake, chunking, AMF0)
- Supports publish and play streams
- Callbacks for connect, publish, play, audio/video data, disconnect
- GOP cache for low-latency playback
- FLV file recording
- Authentication callback
- Stream statistics (bitrate, frames, uptime)
- Connection limits, timeouts, ping/pong

## Roadmap
You can View the Roadmap [Here](ROADMAP.md).

## Quick Start

### Build
```bash
./build.sh
```

This builds `librtmp.so` and example binaries `rtmp_server_cpp` and `rtmp_server_c`.

## Windows Support

Windows Support hasn't been added yet, it will be implemented once the Linux Port is fully Working.

### Run Example
```bash
./build/rtmp_server_cpp
# or ./build/rtmp_server_c
```

Server listens on `rtmp://localhost:1935/live/stream`

Test with OBS:
- Server: `rtmp://127.0.0.1/live`
- Stream key: `stream`

Or FFmpeg:
```bash
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://127.0.0.1/live/stream
```

## License
MIT
