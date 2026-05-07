FROM aflplusplus/aflplusplus:latest


RUN apt-get update && apt-get install -y \
    wget \
    zlib1g-dev \
    imagemagick \
    patch \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

# Get libpng1.6.15 source code

RUN wget https://download.sourceforge.net/libpng/libpng-1.6.15.tar.gz \
    && tar xf libpng-1.6.15.tar.gz \
    && cp -r libpng-1.6.15 libpng-1.6.15_vanilla \
    && cp -r libpng-1.6.15 libpng-1.6.15_nosan\
    && cp -r libpng-1.6.15 libpng-1.6.15_crash\
    && rm libpng-1.6.15.tar.gz

# Copy relevant files
COPY seeds/   /work/seeds/
COPY src/     /work/src/
COPY Makefile /work/Makefile
COPY changes.patch /work/changes.patch
COPY crash.patch /work/crash.patch
COPY png.dict /work/png.dict
COPY crash/default/crashes/ /work/crash/
COPY Makefile /work/Makefile


RUN mkdir -p /work/findings
RUN mkdir -p /work/findings-qemu
RUN mkdir -p /work/findings-persistent
RUN mkdir -p /work/findings-nosan

# Run patch to bypass security checks
WORKDIR /work

RUN patch -p1 -d libpng-1.6.15 < changes.patch
RUN patch -p1 -d libpng-1.6.15_vanilla < changes.patch
RUN patch -p1 -d libpng-1.6.15_nosan < changes.patch
RUN patch -p1 -d libpng-1.6.15_crash < crash.patch


WORKDIR /work/libpng-1.6.15

RUN CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-fsanitize=address -g -O1 -fno-omit-frame-pointer" \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared --prefix=$(pwd)/install \
    && make -j$(nproc) \
    && make install

WORKDIR /work/libpng-1.6.15_vanilla

RUN CC=gcc \
    CFLAGS="-g -O1 -fno-omit-frame-pointer" \
    ./configure --disable-shared --prefix=$(pwd)/install_vanilla \
    && make -j$(nproc) \
    && make install

WORKDIR /work/libpng-1.6.15_nosan

RUN CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-g -O1 -fno-omit-frame-pointer" \
    ./configure --disable-shared --prefix=$(pwd)/install_nosan \
    && make -j$(nproc) \
    && make install

WORKDIR /work/libpng-1.6.15_crash

RUN CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-fsanitize=address -g -O1 -fno-omit-frame-pointer" \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared --prefix=$(pwd)/install_crash \
    && make -j$(nproc) \
    && make install

WORKDIR /work

CMD ["/bin/bash"]
