FROM gitpod/workspace-full

RUN sudo apt-get update \
 && sudo apt-get install -y \
    build-essential \
    libtool \
    autotools-dev \
    automake \
    pkg-config \
    libssl-dev \
    libevent-dev \
    bsdmainutils \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-chrono-dev \
    libboost-program-options-dev \
    libboost-test-dev \
    libboost-thread-dev \
    libdb-dev \
    libdb++-dev \
 && sudo rm -rf /var/lib/apt/lists/* \
 && sudo ./autogen.sh \
 && sudo ./configure \
 && make \
 && make install

