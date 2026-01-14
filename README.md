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

#### Dependencies:
EasyRTMP, libav* libraries
