# =================================================================
# CS-412 Fuzzing Lab — Dockerfile
# Target: libpng 1.6.15 with AFL++
#
# Builds 4 harness variants:
#   png_fuzz            — instrumented + ASan  (white-box campaign)
#   png_fuzz_nosan      — instrumented, no ASan (perf benchmark)
#   png_fuzz_qemu       — vanilla gcc, no instrumentation (QEMU -Q)
#   png_fuzz_persistent — instrumented + ASan + persistent mode
# =================================================================

FROM aflplusplus/aflplusplus:latest

# --- Dependencies ------------------------------------------------
# zlib1g-dev : libpng depends on zlib for IDAT decompression
# wget       : download libpng source tarball
# imagemagick: generate diverse seed PNGs
# -----------------------------------------------------------------
RUN apt-get update && apt-get install -y \
    wget \
    zlib1g-dev \
    imagemagick \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

# --- Download libpng 1.6.15 -------------------------------------
# Why 1.6.15?
#   1. Active release with modern code paths (interlacing, transforms)
#   2. Uses restrict-pointer API (png_structrp) internally
#   3. Exercises full zlib inflate pipeline via IDAT chunks
# -----------------------------------------------------------------
RUN wget https://download.sourceforge.net/libpng/libpng-1.6.15.tar.gz \
    && tar xf libpng-1.6.15.tar.gz \
    && rm libpng-1.6.15.tar.gz

# --- Copy project files into container ---------------------------
COPY seeds/   /work/seeds/
COPY src/     /work/src/
COPY Makefile /work/Makefile
COPY changes.patch /work/changes.patch

# --- Step 1: Apply CRC patch ------------------------------------
# PNG chunks carry CRC-32 checksums. Without this patch, every
# mutation that touches chunk data is rejected at the CRC check
# before reaching deeper parsing code. Path discovery plateaus
# almost immediately.
#
# The patch makes png_crc_finish() return 0 unconditionally,
# so libpng processes mutated data regardless of CRC validity.
#
# NOTE: The AFL++ bundled patch targets 1.2.x; for 1.6.15 we use
# a Python fallback that patches pngrutil.c directly.
# -----------------------------------------------------------------
WORKDIR /work
########################## Changed
RUN patch -p0 < changes.patch
# --- Step 2: Build instrumented + ASan libpng --------------------
# CC=afl-clang-fast  → compile-time edge instrumentation
# -fsanitize=address → ASan: detects heap/stack overflow, UAF, etc.
# -g                 → debug symbols for readable ASan stack traces
# -O1                → mild optimization (O0 too slow, O2 may hide bugs)
# --disable-shared   → static linking avoids LD_LIBRARY_PATH issues
# -----------------------------------------------------------------

WORKDIR /work/libpng-1.6.15

RUN CC=afl-clang-fast \
    CFLAGS="-fsanitize=address -g -O1" \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared --prefix=$(pwd)/install \
    && make -j$(nproc) \
    && make install



# --- Step 5: Compile all harness variants ------------------------
WORKDIR /work

# 5a. Instrumented + ASan (main campaign)
RUN afl-clang-fast src/harness.c \
    -I./libpng-1.6.15/install/include \
    -L./libpng-1.6.15/install/lib \
    -lpng16 -lz -lm \
    -fsanitize=address -g -O1 \
    -o png_fuzz




CMD ["/bin/bash"]
