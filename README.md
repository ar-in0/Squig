# SQUIG
See [Issues](https://github.com/ar-in0/Squig/issues)  

#### Quick Start: 
```bash
git clone https://github.com/ar-in0/Squig
cd Squig
./setup.sh

# To compile
cmake --build ./build

# Run Server
./build/squig

# Test with RTMP client streaming over localhost
ffmpeg -re -i assets/test0_1080_30_squig_base.mp4 -c:v libx264 -c:a aac -f flv rtmp://localhost/live/stream
```

#### Dependencies:
EasyRTMP, libav* libraries, OpenCV 4.x
