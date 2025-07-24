FROM ubuntu:20.04
LABEL maintainer="falkTX <falktx@falktx.com>"
ENV DEBIAN_FRONTEND noninteractive

# enable i386
RUN dpkg --add-architecture i386

# update system
RUN apt-get update -qq && apt-get upgrade -qqy && apt-get clean

# install packages needed for build
RUN apt-get install -qqy --no-install-recommends ca-certificates gcc-multilib git make openssl wget && \
    apt-get clean

# install newer wine
RUN mkdir -pm755 /etc/apt/keyrings && \
    wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key && \
    wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/focal/winehq-focal.sources

RUN apt-get update -qq && \
    apt-get install -qqy --no-install-recommends winehq-stable wine-stable-dev && \
    apt-get clean

# fetch wineasio
RUN git clone https://github.com/wineasio/wineasio.git --depth=1

# build wineasio
WORKDIR /wineasio
RUN make 32 64

# CMD for inspection
CMD ["bash"]
