FROM gitpod/workspace-full

RUN sudo add-apt-repository universe 

RUN sudo apt-get update \
 && sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils \
 && sudo rm -rf /var/lib/apt/lists/*
 
RUN sudo apt-get install -y libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev

RUN sudo apt-get install -y libdb-dev \
 && sudo apt-get install -y libdb++-dev
 
RUN sudo ./autogen.sh \
 && sudo ./configure \
 && make \
 && make install

