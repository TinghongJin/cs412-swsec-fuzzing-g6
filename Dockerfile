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
COPY patches/ /work/patches/
COPY seeds/   /work/seeds/
COPY src/     /work/src/
COPY Makefile /work/Makefile

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
WORKDIR /work/libpng-1.6.15

RUN cp /AFLplusplus/utils/libpng_no_checksum/libpng-nocrc.patch \
       /work/patches/nocrc.patch 2>/dev/null || true \
    && (patch -p0 < /work/patches/nocrc.patch 2>/dev/null \
        || python3 -c "import re; c=open('pngrutil.c').read(); c=re.sub(r'(png_crc_finish\b[^\n]*\n\{)', r'\1\n   PNG_UNUSED(skip);\n   return 0;', c); open('pngrutil.c','w').write(c); print('CRC patch applied')")

# --- Step 2: Build instrumented + ASan libpng --------------------
# CC=afl-clang-fast  → compile-time edge instrumentation
# -fsanitize=address → ASan: detects heap/stack overflow, UAF, etc.
# -g                 → debug symbols for readable ASan stack traces
# -O1                → mild optimization (O0 too slow, O2 may hide bugs)
# --disable-shared   → static linking avoids LD_LIBRARY_PATH issues
# -----------------------------------------------------------------
RUN CC=afl-clang-fast \
    CFLAGS="-fsanitize=address -g -O1" \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared --prefix=$(pwd)/install \
    && make -j$(nproc) \
    && make install

# --- Step 3: Build instrumented, no-ASan libpng (perf baseline) --
RUN make distclean \
    && CC=afl-clang-fast CFLAGS="-g -O1" \
    ./configure --disable-shared --prefix=$(pwd)/install_nosan \
    && make -j$(nproc) \
    && make install

# --- Step 4: Build vanilla libpng (QEMU mode) -------------------
RUN make distclean \
    && CC=gcc CFLAGS="-g -O1" \
    ./configure --disable-shared --prefix=$(pwd)/install_vanilla \
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

# 5b. Instrumented, no ASan (perf benchmark)
RUN afl-clang-fast src/harness.c \
    -I./libpng-1.6.15/install_nosan/include \
    -L./libpng-1.6.15/install_nosan/lib \
    -lpng16 -lz -lm \
    -g -O1 \
    -o png_fuzz_nosan

# 5c. Vanilla / no instrumentation (QEMU mode)
RUN gcc src/harness.c \
    -I./libpng-1.6.15/install_vanilla/include \
    -L./libpng-1.6.15/install_vanilla/lib \
    -lpng16 -lz -lm \
    -g -O1 \
    -o png_fuzz_qemu

# 5d. Persistent mode + ASan (perf benchmark)
RUN afl-clang-fast src/harness_persistent.c \
    -I./libpng-1.6.15/install/include \
    -L./libpng-1.6.15/install/lib \
    -lpng16 -lz -lm \
    -fsanitize=address -g -O1 \
    -o png_fuzz_persistent

# --- Step 6: Copy dictionary ------------------------------------
RUN cp /AFLplusplus/dictionaries/png.dict /work/png.dict 2>/dev/null || true

# --- Step 7: Populate seeds if empty ----------------------------
RUN if [ -z "$(ls -A /work/seeds/ 2>/dev/null | grep -v README)" ]; then \
        cp /AFLplusplus/testcases/images/png/* /work/seeds/ 2>/dev/null || true; \
    fi

CMD ["/bin/bash"]
