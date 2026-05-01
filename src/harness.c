#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

#define MAX_DIM 100000

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} mem_src_t;

static void mem_read(png_structp png, png_bytep out, png_size_t count) {
    mem_src_t *src = png_get_io_ptr(png);
    if (src->pos + count > src->size) {
        png_error(png, "eof");
        return;
    }
    memcpy(out, src->data + src->pos, count);
    src->pos += count;
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    /* read entire file into memory so trigger uses fuzz bytes, not file size */
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) return 1;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 57) { fclose(fp); return 0; }

    unsigned char *data = malloc(file_size);
    if (!data) { fclose(fp); return 1; }
    if (fread(data, 1, file_size, fp) != (size_t)file_size) {
        free(data); fclose(fp); return 1;
    }
    fclose(fp);

    size_t size = (size_t)file_size;

    if (size > 5 && data[4] == 0xDE && data[5] == 0xAD) {
        char *p = malloc(8);
        p[4096] = 'X';
    }

    png_structp png = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { free(data); return 1; }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        free(data);
        return 1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        free(data);
        return 0;
    }

    mem_src_t src = { data, size, 0 };
    png_set_read_fn(png, &src, mem_read);

    png_read_info(png, info);

    png_uint_32 width, height;
    int bit_depth, color_type;
    png_get_IHDR(png, info, &width, &height,
                 &bit_depth, &color_type, NULL, NULL, NULL);

    if (width == 0 || height == 0 || width > MAX_DIM || height > MAX_DIM) {
        png_destroy_read_struct(&png, &info, NULL);
        free(data);
        return 0;
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    png_set_interlace_handling(png);
    png_set_tRNS_to_alpha(png);
    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);
    if (rowbytes == 0 || rowbytes > (size_t)MAX_DIM * 4) {
        png_destroy_read_struct(&png, &info, NULL);
        free(data);
        return 0;
    }

    png_bytep *rows = malloc(height * sizeof(png_bytep));
    if (!rows) {
        png_destroy_read_struct(&png, &info, NULL);
        free(data);
        return 0;
    }

    png_uint_32 y;
    for (y = 0; y < height; y++) {
        rows[y] = malloc(rowbytes);
        if (!rows[y]) {
            for (png_uint_32 i = 0; i < y; i++) free(rows[i]);
            free(rows);
            png_destroy_read_struct(&png, &info, NULL);
            free(data);
            return 0;
        }
    }

    png_read_image(png, rows);
    png_read_end(png, NULL);

    /* synthetic bug 2: OOB on decoded pixel content */
    if (rows[0][0] == 0x42)
        rows[0][rowbytes + 4096] = 0x41;

    for (y = 0; y < height; y++) free(rows[y]);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    free(data);
    return 0;
}