#!/bin/bash
set -e

sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev libavcodec-dev libavformat-dev libavdevice-dev libavfilter-dev

git submodule update --init --recursive

mkdir -p build
cmake -S . -B ./build