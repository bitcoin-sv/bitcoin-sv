FROM gitpod/workspace-full

RUN sudo apt-get update \
 && sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils \
 && sudo rm -rf /var/lib/apt/lists/*

RUN sudo add-apt-repository universe
RUN sudo apt-get update
RUN sudo apt-get install -y libboost-all-dev

RUN sudo apt-get install -y libdb-dev
RUN sudo apt-get install -y libdb++-dev

# Build BitCoin
#RUN cd /workspace
#RUN ./autogen.sh
#RUN ./configure
#RUN sudo make
#RUN sudo make install

