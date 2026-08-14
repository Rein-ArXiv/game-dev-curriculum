#!/usr/bin/env bash
# WSL2 또는 Linux에서 system_kernel_lab 빌드에 필요한 패키지를 설치한다.
# Ubuntu 22.04+ / Debian 12+ 기준.
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libsdl2-dev \
    libgl1-mesa-dev \
    liburing-dev

# 측정 도구. 다음 단계들에서 사용.
sudo apt-get install -y \
    strace \
    linux-tools-generic \
    trace-cmd

echo "done. now: cmake -S . -B build && cmake --build build -j"
