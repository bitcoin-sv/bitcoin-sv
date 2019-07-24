#Dockerfile for building Bitcoin SV
FROM ubuntu:bionic
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
       python3-pip      \
       perl             \
       cpanminus        \
       libperlio-gzip-perl \
       libjson-perl     \
       zlib1g-dev       \
       bsdmainutils


RUN pip3 install requests   
RUN cpanm PerlIO::gzip
RUN cpanm JSON
RUN cpanm Term::ReadLine
RUN git clone https://github.com/linux-test-project/lcov.git && cd lcov && make install

COPY contrib/nchain/devops/* ./
RUN  chmod +x /entrypoint.py
RUN  useradd -G users jenkins
USER jenkins

