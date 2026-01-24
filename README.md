# SQUIG
Quick Start: 
```bash
git clone https://github.com/ar-in0/Squig
git submodule update --init --recursive # get latest submodule sources

# libav* libs are dependencies of each other.
sudo apt install -y libavcodec-dev libavformat-dev libavdevice-dev libavfilter-dev

cd Squig
mkdir build
cmake -S . -B ./build
cmake --build ./build
```

Testing
Initial server testing: Use ffmpeg on client
```bash
ffmpeg -re -i assets/test0_1080_30_squig_base.mp4 -c:v libx264 -c:a aac -f flv rtmp://localhost/live/stream
```

#### Dependencies:
EasyRTMP, libav* libraries, OpenCV 4.x
