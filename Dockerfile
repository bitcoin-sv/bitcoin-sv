#Dockerfile for building Bitcoin SV
FROM ubuntu:cosmic
LABEL maintainer=p.foster@nchain.com
RUN apt-get update &&   \
    apt-get -y install  \
       libboost-all-dev \
       libdb-dev        \
       libdb++-dev      \
       build-essential  \
       libtool          \
       autotools-dev    \
       automake         \
       pkg-config       \
       libssl-dev       \
       libevent-dev     \
       libminiupnpc-dev \
       libzmq3-dev      \
       git              \
       python3          \
       perl             \
       cpanminus        \
       bsdmainutils
    
RUN cpanm install PerlIO::gzip
RUN cpanm install JSON
RUN git clone https://github.com/linux-test-project/lcov.git && cd lcov && make install
COPY ./entrypoint.py .
RUN  chmod +x /entrypoint.py
RUN  useradd -G users jenkins
USER jenkins

