
#include <stdio.h>
#include <stdlib.h>
#include <png.h>


#define MAX_DIM 4096

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <png_file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp)
        return 1;

    // read，check if it's a png
    png_structp png = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return 1;
    }

    // png structure
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return 1;
    }

    
    // handle jmp error
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    // png_set_crc_action(png, PNG_CRC_QUIET_USE, PNG_CRC_QUIET_USE);

    
    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 width  = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);

    // check !=0
    if (width == 0 || height == 0 || width > MAX_DIM || height > MAX_DIM) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    // Transform
    png_set_expand(png);
    // png_set_strip_16(png);
    // png_set_gray_to_rgb(png);
    png_set_tRNS_to_alpha(png);
    png_set_interlace_handling(png);

    
    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);
    // read rowbytes, check
    if (rowbytes == 0) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

   
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


    for (png_uint_32 y = 0; y < height; y++)
        png_read_row(png, row_pointers[y], NULL);
    // png_read_image(png, row_pointers);
    png_read_end(png, NULL);

    for (png_uint_32 y = 0; y < height; y++)
        free(row_pointers[y]);
    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return 0;
}
