#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <png.h>

#define PNG_LIB_STR PNG_LIBPNG_VER_STRING
#define CONFIG_SIZE 32

#define PNG_MAX_PIXELS 262144
#define PNG_MAX_WIDTH  4096
#define PNG_MAX_HEIGHT 4096

struct png_read_handle {
    const png_byte *data;
    const png_size_t size;
    png_size_t offset;
};

void png_read_callback_fn(png_structp png, png_bytep out_data, png_size_t length) {
    struct png_read_handle *handle = (struct png_read_handle *)png_get_io_ptr(png);
    if (handle->offset + length > handle->size)
        png_error(png, "read past end of input");
    memcpy(out_data, handle->data + handle->offset, length);
    handle->offset += length;
}

void my_fuzz_transform_callback(png_structp png_ptr, png_row_infop row_info, png_bytep data) {
    if (data == NULL || row_info->rowbytes == 0) return;
    uint8_t *user_config = (uint8_t *)png_get_user_transform_ptr(png_ptr);
    if (!user_config) return;
    uint8_t op = user_config[13] % 3;
    switch (op) {
        case 0: data[0] ^= 0xFF; break;
        case 1: data[row_info->rowbytes - 1] = 0; break;
        case 2: break;
    }
}

void run_png_set(png_structp png, png_infop info, uint8_t config[CONFIG_SIZE]) {
    uint32_t flags = *(uint32_t *)&config[0];
    if (flags & (1 << 0)) png_set_strip_16(png);
    if (flags & (1 << 1)) png_set_strip_alpha(png);
    if (flags & (1 << 2)) {
        png_color palette[256];
        png_uint_16 histogram[256];
        memset(palette, 0, sizeof(palette));
        memset(histogram, 0, sizeof(histogram));
        png_set_dither(png, palette, 256, config[4], histogram, config[5] % 2);
    }
    if (flags & (1 << 3)) {
        uint16_t raw_scrn, raw_file;
        memcpy(&raw_scrn, &config[6], 2);
        memcpy(&raw_file, &config[8], 2);
        double scrn_gamma = 0.1 + ((double)raw_scrn / 65535.0) * 9.9;
        double file_gamma = 0.1 + ((double)raw_file / 65535.0) * 9.9;
        png_set_gamma(png, scrn_gamma, file_gamma);
    }
    if (flags & (1 << 4)) png_set_expand(png);
    if (flags & (1 << 5)) png_set_palette_to_rgb(png);
    if (flags & (1 << 6)) png_set_expand_gray_1_2_4_to_8(png);
    if (flags & (1 << 7)) png_set_gray_1_2_4_to_8(png);
    if (flags & (1 << 8)) png_set_tRNS_to_alpha(png);
    if (flags & (1 << 9)) png_set_gray_to_rgb(png);
    if (flags & (1 << 10)) {
        uint8_t raw_err   = config[10];
        uint8_t raw_red   = config[11];
        uint8_t raw_green = config[12];
        int error_action = (raw_err % 3) + 1;
        if (raw_red < 25) {
            png_set_rgb_to_gray(png, error_action, -1.0, -1.0);
        } else if (raw_red < 50) {
            png_set_rgb_to_gray_fixed(png, error_action, -1, -1);
        } else {
            double red_f   = (double)raw_red   / 255.0;
            double green_f = (double)raw_green / 255.0;
            if (flags & (1 << 11)) {
                png_set_rgb_to_gray(png, error_action, red_f, green_f);
            } else {
                png_set_rgb_to_gray_fixed(png, error_action,
                    (png_fixed_point)(red_f   * 100000.0),
                    (png_fixed_point)(green_f * 100000.0));
            }
        }
    }
    if (flags & (1 << 11)) {
        png_set_read_user_transform_fn(png, my_fuzz_transform_callback);
        png_set_user_transform_info(png, config, 8, 3);
    }
    if (flags & (1 << 12)) {
        png_color_16 bg_color;
        png_colorp palette;
        int num_palette = 0;
        if (png_get_PLTE(png, info, &palette, &num_palette) && num_palette > 0) {
            bg_color.index = config[14] % num_palette;
        } else {
            bg_color.index = 0;
        }
        bg_color.red   = (config[15] << 8) | config[16];
        bg_color.green = (config[17] << 8) | config[18];
        bg_color.blue  = (config[19] << 8) | config[20];
        bg_color.gray  = (config[21] << 8) | config[22];
        int gamma_code  = config[23] % 4;
        int need_expand = config[24] % 2;
        double bg_gamma = (double)(config[25] + 1) / 100.0;
        png_set_background(png, &bg_color, gamma_code, need_expand, bg_gamma);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) return 1;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *buf = malloc((size_t)fsize);
    if (!buf) { fclose(fp); return 1; }

    if (fread(buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(buf); fclose(fp); return 1;
    }
    fclose(fp);

    int len = (int)fsize;

    png_uint_32 height = 0;
    png_uint_32 width  = 0;
    png_bytep * volatile row_pointers = NULL;
    png_bytep   volatile image_data   = NULL;
    png_structp png  = NULL;
    png_infop   info = NULL;
    uint8_t config[CONFIG_SIZE];

    if (len <= CONFIG_SIZE) goto done;
    memcpy(config, buf, CONFIG_SIZE);

    png = png_create_read_struct(PNG_LIB_STR, NULL, NULL, NULL);
    if (!png) goto done;
    info = png_create_info_struct(png);
    if (!info) goto cleanup;

    if (setjmp(png_jmpbuf(png))) goto cleanup;

    struct png_read_handle read_handle = {
        .data   = buf + CONFIG_SIZE,
        .size   = (png_size_t)len - CONFIG_SIZE,
        .offset = 0
    };
    png_set_read_fn(png, &read_handle, png_read_callback_fn);

    png_read_info(png, info);

    width  = png_get_image_width(png, info);
    height = png_get_image_height(png, info);

    if (!width || !height || width > PNG_MAX_WIDTH || height > PNG_MAX_HEIGHT ||
        width * height > PNG_MAX_PIXELS)
        goto cleanup;

    run_png_set(png, info, config);
    png_read_update_info(png, info);

    size_t rowbytes = png_get_rowbytes(png, info);
    size_t max_safe_rowbytes = width * 8;
    rowbytes = (rowbytes > max_safe_rowbytes) ? rowbytes : max_safe_rowbytes;

    row_pointers = (png_bytep *)png_malloc(png, height * sizeof(png_bytep));
    if ((size_t)height > PNG_SIZE_MAX / rowbytes) {
        png_free(png, row_pointers);
        png_error(png, "image_data buffer too large");
    }
    image_data = (png_bytep)png_malloc(png, height * rowbytes);
    for (png_uint_32 i = 0; i < height; i++)
        row_pointers[i] = image_data + i * rowbytes;

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);

cleanup:
    if (image_data)   png_free(png, image_data);
    if (row_pointers) png_free(png, row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
done:
    free(buf);
    return 0;
}
