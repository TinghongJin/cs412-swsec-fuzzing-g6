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
    && rm libpng-1.6.15.tar.gz

# Copy relevant files
COPY seeds/   /work/seeds/
COPY src/     /work/src/
COPY Makefile /work/Makefile
COPY changes.patch /work/changes.patch
COPY png.dict /work/png.dict

# Run patch to bypass security checks
WORKDIR /work

RUN patch -p1 -d libpng-1.6.15 < changes.patch


WORKDIR /work/libpng-1.6.15

RUN CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-fsanitize=address -g -O1" \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared --prefix=$(pwd)/install \
    && make -j$(nproc) \
    && make install

WORKDIR /work

# 5a. Instrumented + ASan (main campaign)
RUN afl-clang-fast src/harness.c \
    -Ilibpng-1.6.15/install/include \
    -Llibpng-1.6.15/install/lib \
    -lpng16 -lz -lm \
    -fsanitize=address -g -O1 \
    -o png_fuzz




CMD ["/bin/bash"]
