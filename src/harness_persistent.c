/*
 * CS-412 Fuzzing Lab — libpng 1.6.15 Persistent Mode Harness
 * Author: Member B
 *
 * Difference from harness.c:
 *   Fork mode:  AFL++ forks a new process for every test case.
 *   Persistent: the harness loops inside the same process (N=10000
 *               iterations), then exits and AFL++ restarts it.
 *               This amortises fork overhead → 2–20× speedup.
 *
 * Additional difference: input comes from a shared-memory buffer
 * (__AFL_FUZZ_TESTCASE_BUF) instead of a file, so we need a custom
 * read callback for libpng (png_set_read_fn).
 *
 * Trade-off: if the library leaks state between iterations (global
 * variables, cached allocations), behaviour may drift, lowering
 * stability. Reduce N or add more cleanup if stability drops.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <png.h>

#define MAX_DIM 4096

/* --- Memory-based read callback --------------------------------
 * libpng normally reads from a FILE*. In persistent mode the input
 * is in a memory buffer, so we provide a custom reader.
 *
 * png_set_read_fn(png, &reader, read_from_memory) tells libpng:
 *   "whenever you need bytes, call read_from_memory instead of fread"
 * ---------------------------------------------------------------- */
struct mem_reader {
    const unsigned char *data;   /* AFL++ shared-memory buffer */
    png_size_t           size;   /* total input length */
    png_size_t           offset; /* current read position */
};

static void read_from_memory(png_structp png, png_bytep out,
                             png_size_t count) {
    struct mem_reader *r = (struct mem_reader *)png_get_io_ptr(png);
    if (r->offset + count > r->size) {
        png_error(png, "read past end of input");
        return; /* png_error longjmps; this line is never reached */
    }
    memcpy(out, r->data + r->offset, count);
    r->offset += count;
}

/* --- AFL++ persistent-mode macros ------------------------------
 * __AFL_FUZZ_INIT()  : declares shared-memory variables (file scope)
 * __AFL_INIT()       : defers fork-server start to this point
 * __AFL_LOOP(N)      : loops N times inside the same process
 * __AFL_FUZZ_TESTCASE_BUF : pointer to current input bytes
 * __AFL_FUZZ_TESTCASE_LEN : length of current input
 *
 * These macros are resolved at compile time by afl-clang-fast.
 * With plain gcc they expand to no-ops.
 * ---------------------------------------------------------------- */
__AFL_FUZZ_INIT();

int main(int argc, char **argv) {
    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;

        /* PNG signature is 8 bytes; anything shorter is useless */
        if (len < 8)
            continue;

        png_structp png = png_create_read_struct(
            PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png)
            continue;

        png_infop info = png_create_info_struct(png);
        if (!info) {
            png_destroy_read_struct(&png, NULL, NULL);
            continue;
        }

        if (setjmp(png_jmpbuf(png))) {
            png_destroy_read_struct(&png, &info, NULL);
            continue;
        }

        /* Use memory reader instead of file I/O */
        struct mem_reader reader = {
            .data   = buf,
            .size   = (png_size_t)len,
            .offset = 0
        };
        png_set_read_fn(png, &reader, read_from_memory);

        png_read_info(png, info);

        png_uint_32 w = png_get_image_width(png, info);
        png_uint_32 h = png_get_image_height(png, info);
        if (w > MAX_DIM || h > MAX_DIM) {
            png_destroy_read_struct(&png, &info, NULL);
            continue;
        }

        png_set_expand(png);
        png_set_strip_16(png);
        png_set_gray_to_rgb(png);
        png_read_update_info(png, info);

        png_size_t rowbytes = png_get_rowbytes(png, info);
        png_bytep *rows = malloc(h * sizeof(png_bytep));
        if (!rows) {
            png_destroy_read_struct(&png, &info, NULL);
            continue;
        }

        int alloc_ok = 1;
        for (png_uint_32 y = 0; y < h; y++) {
            rows[y] = malloc(rowbytes);
            if (!rows[y]) {
                for (png_uint_32 j = 0; j < y; j++)
                    free(rows[j]);
                alloc_ok = 0;
                break;
            }
        }

        if (alloc_ok) {
            png_read_image(png, rows);
            png_read_end(png, NULL);
            for (png_uint_32 y = 0; y < h; y++)
                free(rows[y]);
        }

        free(rows);
        /* Thorough cleanup prevents state leaks between iterations */
        png_destroy_read_struct(&png, &info, NULL);
    }

    return 0;
}
