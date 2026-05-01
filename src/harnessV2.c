#include <stdio.h>
#include <stdlib.h>
#include <png.h>

#define MAX_DIM 100000

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) return 1;

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

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);

    png_read_info(png, info);

    png_uint_32 width, height;
    int bit_depth, color_type;

    png_get_IHDR(png, info,
        &width, &height,
        &bit_depth, &color_type,
        NULL, NULL, NULL);

    // IMPORTANT: don't over-restrict
    if (width == 0 || height == 0 || width > MAX_DIM || height > MAX_DIM) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    /*
     * IMPORTANT: DO MINIMAL NORMALIZATION
     * We intentionally do NOT force palette expansion
     */

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        // DO NOT force expand always — let libpng handle it
        png_set_palette_to_rgb(png);
    }

    // Enable interlace handling (important CVE surface)
    png_set_interlace_handling(png);

    // Keep transparency conversion (safe but useful)
    png_set_tRNS_to_alpha(png);

    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);

    png_bytep *rows = malloc(height * sizeof(png_bytep));
    if (!rows) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = malloc(rowbytes);
        if (!rows[y]) {
            for (png_uint_32 i = 0; i < y; i++)
                free(rows[i]);
            free(rows);
            png_destroy_read_struct(&png, &info, NULL);
            fclose(fp);
            return 0;
        }
    }

    /*
     * IMPORTANT: row-by-row decoding exposes:
     * - png_combine_row
     * - interlace reconstruction bugs
     * - palette index issues
     */
    for (png_uint_32 y = 0; y < height; y++) {
        png_read_row(png, rows[y], NULL);
    }

    png_read_end(png, NULL);

    for (png_uint_32 y = 0; y < height; y++)
        free(rows[y]);
    free(rows);

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return 0;
}