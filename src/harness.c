/*
 * CS-412 Fuzzing Lab — libpng 1.6.15 Fuzzing Harness (Fork Mode)
 * Author: Member B
 *
 * Entry point: png_read_image() — full decode pipeline
 *
 * Why decode?
 *   1. Decoders process untrusted external input (images from the web)
 *   2. Known code paths exercised:
 *        palette expansion (png_set_expand)
 *        text/ancillary chunk parsing
 *        zlib IDAT decompression pipeline
 *   3. The pipeline is the most complex: chunk parse → zlib inflate
 *        → scanline defilter → pixel transform → output
 *
 * Data flow:
 *   AFL++ generates mutated file → argv[1]
 *     → fopen → png_init_io
 *     → png_read_info   (parses IHDR, PLTE, ancillary chunks)
 *     → png_set_expand / strip_16 / gray_to_rgb  (enable transforms)
 *     → png_read_update_info
 *     → png_read_image  (decompress IDAT, defilter, apply transforms)
 *     → png_read_end    (read post-IDAT chunks like tEXt, tIME)
 *     → cleanup
 *
 * Alternatives considered (Q1):
 *   - Encoder (png_write_image): input is structured memory, not file
 *   - Simplified API (png_image_begin_read_from_memory): 1.6+ only
 *   - Progressive API (png_process_data): separate state machine,
 *     worth fuzzing but harder to harness
 *   - Low-level chunk API (png_read_chunk_header): too low-level,
 *     real apps don't call this directly
 */

#include <stdio.h>
#include <stdlib.h>
#include <png.h>

/*
 * Guard: MAX_DIM
 * Fuzzing rationale: the fuzzer may mutate IHDR to claim 65535×65535.
 * libpng would try to allocate 65535 × 65535 × 4 ≈ 16 GB → OOM.
 * The OS kills the process, AFL++ logs a false crash.
 * 4096×4096 covers all interesting code paths while keeping memory
 * usage under 64 MB.
 */
#define MAX_DIM 4096

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <png_file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp)
        return 1;

    png_structp png = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return 1;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return 1;
    }

    /*
     * Guard: setjmp error handler
     * Fuzzing rationale: libpng uses longjmp for errors. Without this
     * setjmp, every malformed input causes an unhandled longjmp → abort
     * → AFL++ records it as a crash → thousands of false positives.
     * With setjmp, we catch the error, clean up, and return 0 (not a
     * crash). Only real memory errors (detected by ASan) are reported.
     */
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);
    png_read_info(png, info);
    unsigned int width;
    unsigned int height;

    png_get_IHDR(png, info,
        &width, &height,
        NULL, NULL, NULL, NULL, NULL);

    if (width == 0 || height == 0 ||
        width > MAX_DIM || height > MAX_DIM) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    /*
     * Transforms — increase code coverage:
     *   png_set_expand()     palette→RGB, tRNS→alpha, 1/2/4→8 bit
     */
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);

    /*
     * Guard: malloc failure check
     * Fuzzing rationale: under ASan the memory budget is tighter.
     * Combined with fuzzer-generated large dimensions, malloc can
     * fail. We must handle it gracefully (return 0) not crash.
     */
    png_bytep *row_pointers = malloc(height * sizeof(png_bytep));
    if (!row_pointers) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    for (png_uint_32 y = 0; y < height; y++) {
        row_pointers[y] = malloc(rowbytes);
        if (!row_pointers[y]) {
            for (png_uint_32 j = 0; j < y; j++)
                free(row_pointers[j]);
            free(row_pointers);
            png_destroy_read_struct(&png, &info, NULL);
            fclose(fp);
            return 0;
        }
    }

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);

    for (png_uint_32 y = 0; y < height; y++)
        free(row_pointers[y]);
    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return 0;
}
