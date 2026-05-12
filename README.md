# Software Security (CS-412) - Fuzzing Lab

This repository contains the source code, experimental data, and final report for the fuzzing lab.

## Repository Structure

```text
swsec-fuzzing/
├── Dockerfile
├── Makefile
│
├── changes.patch
├── convert_seeds.sh
├── png.dict
│
├── report.tex
├── report.pdf
│
├── src/
│   ├── harness.c
│   └── harness_persistent.c
│
├── seeds/
├── seeds_with_config/
│
├── findings/
│   └── default/
│       └── plot_data
│
├── findings-qemu/
│   └── default/
│       └── plot_data
│
├── plot_output/
│   ├── index.html
│   ├── edges.png
│   ├── exec_speed.png
│   ├── high_freq.png
│   └── low_freq.png
│
└── plot_output_qemu/
│   ├── index.html
│   ├── edges.png
│   ├── exec_speed.png
│   ├── high_freq.png
│   └── low_freq.png
```

## Run the fuzzer

```bash
# I'm using the non-desktop docker
docker context use default
docker info

mkdir -p crash/default/crashes/
docker build -t my-fuzzer .

docker run -it --name fuzz my-fuzzer:latest
make

# run the normal version
make fuzz

# and alternatives
make fuzz-persistent
make fuzz-qemu
make fuzz-nosan

```


## Crash triage

To reproduce the crash we found (CVE-2011-2692), run `make fuzz` for at least 30 minutes. Categorize the crashes using the following script:

```bash
TARGET_BIN="./png_fuzz"
CRASH_DIR="findings/default/crashes"
TRIAGE_DIR="triaged_crashes"

mkdir -p "$TRIAGE_DIR"

echo "Categorizing crash files by bug type..."
for crash_file in "$CRASH_DIR"/id*; do
    [ -f "$crash_file" ] || continue
    filename=$(basename "$crash_file")
    bug_type=$(timeout 2 "$TARGET_BIN" "$crash_file" 2>&1 | grep "ERROR: AddressSanitizer:" | head -n 1 | awk '{print $3}')
    if [ -z "$bug_type" ]; then
        bug_type="other_crashes"
    fi
    mkdir -p "$TRIAGE_DIR/$bug_type"
    cp "$crash_file" "$TRIAGE_DIR/$bug_type/"
    echo "[-] categorize: $filename -> $bug_type"
done
echo "Finished! See $TRIAGE_DIR"
```

Now we see catogorized crashes under the triaged_crashes directory.

To check different types of crashes:

```bash
for f in findings/default/crashes/id*; do 
    out=$(timeout 2 ./png_fuzz "$f" 2>&1)
    type=$(echo "$out" | grep "ERROR: AddressSanitizer:" | awk '{print $3}')
    frame=$(echo "$out" | grep -m 1 -oP '(?<=#1 0x[0-9a-f]{12} in ).*?(?= )')
    if [ -n "$type" ]; then echo "$type @ $frame"; fi
done | sort | uniq -c | sort -nr
```

You should see a few crashes at png_read_info of type heap-buffer-overflow. It is rare, so if you don't see it, fuzz longer.

To find the file name of those crashes related to CVE-2011-2692, run the following command:

```bash
for f in triaged_crashes/heap-buffer-overflow/*; do timeout 2 ./png_fuzz "$f" 2>&1 | grep -q "png_read_info" && echo "[!] $f"; done
```

Expected output:

```
[!] triaged_crashes/heap-buffer-overflow/id:000084,sig:06,src:000140,time:186299,execs:161730,op:havoc,rep:2
```

Use this testcase to reproduce the crash.

```bash
./png_fuzz triaged_crashes/heap-buffer-overflow/id:000084,sig:06,src:000140,time:186299,execs:161730,op:havoc,rep:2
```

Expected output:

```
NOTE: CRC in the file is 0x878b5003, change to 0x51553359
NOTE: CRC in the file is 0x0049454e, change to 0xbc1a9008
=================================================================
==3731807==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000011 at pc 0x560802d1f44f bp 0x7fffbaf4d4d0 sp 0x7fffbaf4d4c8
READ of size 1 at 0x502000000011 thread T0
    #0 0x560802d1f44e in png_handle_sCAL /work/libpng-1.2.27/pngrutil.c:1750:8
    #1 0x560802ce5fc2 in png_read_info /work/libpng-1.2.27/pngread.c:491:10
    #2 0x560802cd6f16 in main /work/src/harness.c:158:5
    #3 0x7fd1e44221c9  (/lib/x86_64-linux-gnu/libc.so.6+0x2a1c9) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #4 0x7fd1e442228a in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x2a28a) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #5 0x560802bf44d4 in _start (/work/png_fuzz+0x314d4) (BuildId: 5a462f486ac9c2653b49e7d5546d542781a84288)
```

The crash is a heap-buffer-overflow in `png_handle_sCAL`.

Before minimizing the test case, go to libpng and patch CVE-2011-2501. Otherwise the minimized test case can only hit that bug.

```bash
cd libpng-1.2.27
patch < ../fix-global-buffer-overflow.patch

# recompile the library
CC=afl-clang-fast \
    CXX=afl-clang-fast++ \
    CFLAGS="-fsanitize=address -g -O1 -fno-omit-frame-pointer" \
    LDFLAGS="-fsanitize=address" \
    ./configure --disable-shared  --prefix=$(pwd)/install \
    && printf '\nlibpng.vers:\n\t@true\n' >> Makefile \
    && make -j$(nproc) \
    && make install

cd ..

make clean
make
```

Now minimize the testcase:

```bash
afl-tmin -i <path-to-the-testcase> -o ./minimized_crash.png -- ./png_fuzz @@
```

And reproduce the crash using the minimized testcase:

```bash
./png_fuzz ./minimized_crash.png
```

Expected output:

```
NOTE: CRC in the file is 0x30303030, change to 0xb8d050b4
NOTE: CRC in the file is 0x30303030, change to 0xa6fa7b23
NOTE: CRC in the file is 0x30303030, change to 0x58e46a74
NOTE: CRC in the file is 0x30303030, change to 0xbc1a9008
=================================================================
==3735109==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000011 at pc 0x5570ab4d044f bp 0x7ffc8c30a6d0 sp 0x7ffc8c30a6c8
READ of size 1 at 0x502000000011 thread T0
    #0 0x5570ab4d044e in png_handle_sCAL /work/libpng-1.2.27/pngrutil.c:1750:8
    #1 0x5570ab496fc2 in png_read_info /work/libpng-1.2.27/pngread.c:491:10
    #2 0x5570ab487f16 in main /work/src/harness.c:158:5
    #3 0x7fb76b5b81c9  (/lib/x86_64-linux-gnu/libc.so.6+0x2a1c9) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #4 0x7fb76b5b828a in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x2a28a) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #5 0x5570ab3a54d4 in _start (/work/png_fuzz+0x314d4) (BuildId: 953a1275756208f08d04983aba76adb89b1712a9)
...
```
Note that it is still a heap buffer overflow in `png_handle_sCAL`, instead of a global buffer overflow in `png_format_buffer`.

Don't forget to remove the patch if you are going to reproduce our fuzz-persistent/qemu/nosan.
