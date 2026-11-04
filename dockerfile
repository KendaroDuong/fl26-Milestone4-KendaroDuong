FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        python3-pip \
        python3-dev \
        valgrind \
        lcov \
        doxygen \
        clang \
        clang-tidy \
        libxml2-dev \
        libxslt1-dev \
        libgl1-mesa-dev \
        libglu1-mesa-dev \
        qt6-base-dev \
        qt6-base-dev-tools \
        x11-apps \
        git && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Windows Docker Desktop -> host X server
ENV DISPLAY=host.docker.internal:0

# Prevent Qt XDG_RUNTIME_DIR warning
ENV XDG_RUNTIME_DIR=/tmp/runtime-root

RUN mkdir -p /tmp/runtime-root && \
    chmod 700 /tmp/runtime-root