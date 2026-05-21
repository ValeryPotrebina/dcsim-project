FROM ubuntu:18.04

# Avoid interactive prompts during apt install
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    gcc-6 \
    g++-6 \
    autoconf \
    automake \
    libtool \
    make \
    git \
    vim \
    python3 \
    python3-pip \
    python3-matplotlib \
    python3-numpy \
    && rm -rf /var/lib/apt/lists/*

# Make GCC 6 the default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-6 60 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-6

WORKDIR /workspace

CMD ["/bin/bash"]
