FROM aflplusplus/aflplusplus:latest


RUN apt-get update && apt-get install -y \
    wget zlib1g-dev imagemagick patch gcc \
    autoconf automake libtool \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work


# Add libpng for crash if needed
RUN wget https://download.sourceforge.net/libpng/libpng-1.2.27.tar.gz \
    && tar xf libpng-1.2.27.tar.gz \
    && cp -r libpng-1.2.27 libpng-1.2.27_vanilla \
    && cp -r libpng-1.2.27 libpng-1.2.27_nosan \ 
    && rm libpng-1.2.27.tar.gz

# Copy relevant files
COPY seeds/   /work/seeds/
COPY src/     /work/src/
COPY Makefile /work/Makefile
COPY changes.patch /work/changes.patch
COPY png.dict /work/png.dict
COPY crash/default/crashes/ /work/crash/
COPY Makefile /work/Makefile


RUN mkdir -p /work/findings
RUN mkdir -p /work/findings-qemu
RUN mkdir -p /work/findings-persistent
RUN mkdir -p /work/findings-nosan

# Run patch to bypass security checks
WORKDIR /work

RUN patch -p1 -d libpng-1.2.27 < changes.patch
RUN patch -p1 -d libpng-1.2.27_vanilla < changes.patch
RUN patch -p1 -d libpng-1.2.27_nosan < changes.patch



WORKDIR /work/libpng-1.2.27
# Building old version, use autoreconf to generate configure script
# The generated Makefile is not correct, so we need to add a dummy rule to bypass the version check
RUN autoreconf -fiv
RUN CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-fsanitize=address -g -O1 " \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared  --prefix=$(pwd)/install \
    && printf '\nlibpng.vers:\n\t@true\n' >> Makefile \
    && make -j$(nproc) \
    && make install

WORKDIR /work/libpng-1.2.27_vanilla
RUN autoreconf -fiv
RUN CC=gcc \
    CFLAGS="-g -O1 -fno-omit-frame-pointer" \
    ./configure --disable-shared --prefix=$(pwd)/install_vanilla \
    && printf '\nlibpng.vers:\n\t@true\n' >> Makefile \
    && make -j$(nproc) \
    && make install

WORKDIR /work/libpng-1.2.27_nosan
RUN autoreconf -fiv
RUN CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-g -O1 -fno-omit-frame-pointer" \
    ./configure --disable-shared --prefix=$(pwd)/install_nosan \
    && printf '\nlibpng.vers:\n\t@true\n' >> Makefile \
    && make -j$(nproc) \
    && make install

# WORKDIR /work/libpng-1.2.27_crash
# RUN autoreconf -fiv
# RUN CC=afl-clang-fast \
#     CXX=afl-clang-fast++ \
#     CFLAGS="-fsanitize=address -g -O1 -fno-omit-frame-pointer" \
#     LDFLAGS="-fsanitize=address" \
#     ./configure --disable-shared --prefix=$(pwd)/install_crash \
#     && printf '\nlibpng.vers:\n\t@true\n' >> Makefile \
#     && make -j$(nproc) \
#     && make install

WORKDIR /work

CMD ["/bin/bash"]
