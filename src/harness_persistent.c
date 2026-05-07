/*
    Source:
    - AFL setup (e.g. __AFL_FUZZ_INIT): exercise-fuzzing.pdf
    - How to read png: https://www.libpng.org/pub/png/libpng-manual.txt
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <png.h>
#include <assert.h>

// Uncomment this line to support reading from file during the debug phase
// #define DEBUG_FILE_INPUT


#define PNG_LIB_STR PNG_LIBPNG_VER_STRING

// We limit width x height so that we have better flexibililty
#define PNG_MAX_PIXELS 262144
#define PNG_MAX_WIDTH 4096
#define PNG_MAX_HEIGHT 4096

// Custom png read handler.
struct png_read_handle {
    const png_byte* data;
    const png_size_t size;
    png_size_t offset;
};

// The callback function provided to png_set_read_fn(). Copies data from afl buffer to *data. Updates the read handler.
void png_read_callback_fn(png_struct *png, png_byte *out_data, png_size_t length){
    struct png_read_handle* handle = (struct png_read_handle*)png_get_io_ptr(png);
    if (handle->offset + length > handle->size) png_error(png, "read past end of input");
    memcpy(out_data, handle->data + handle->offset, length);
    handle->offset += length;
    // fprintf(stderr, "Callback got data, first byte: %02X\n", out_data[0]);
}

// Support debugging with a file input (instead of AFL buffer in memory)
void load_file_to_buf(char* filename, unsigned char **buf, int* len){
    FILE *fp = fopen(filename, "rb");
    assert(fp);

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    *buf = (unsigned char *)malloc(file_size);
    assert(buf);
    
    size_t read_bytes = fread(*buf, 1, file_size, fp);
    assert(read_bytes == file_size);

    *len = read_bytes;

    fclose(fp);
    printf("load finished, read %d bytes\n", *len);
    
    return;
}

// debug output
void print_image_bytes(png_bytep* row_pointers, png_uint_32 height, png_uint_32 rowbytes){
    for (png_uint_32 i = 0; i < height; i++) {
        if (row_pointers[i] != NULL) {
            for (size_t j = 0; j < rowbytes; j++) {
                printf("%02X ", row_pointers[i][j]); 
            }
        }
        printf("\n");
    }
}

void test_cve_2015_8472(png_struct* png, png_info* info){
    png_byte bit_depth = png_get_bit_depth(png, info);
    png_colorp libpng_palette = NULL;
    int num_palette = 0;
    int ret = png_get_PLTE(png, info, &libpng_palette, &num_palette);
    if (ret & PNG_INFO_PLTE) {
        int max_expected_colors = 1 << bit_depth;
        png_colorp app_palette = (png_colorp)malloc(max_expected_colors * sizeof(png_color));
        if (app_palette){
            memcpy(app_palette, libpng_palette, num_palette * sizeof(png_color));
            if (num_palette > 0) {
                volatile png_byte dummy = app_palette[0].red; 
            }
            free(app_palette);
        }
    }
}

#ifndef DEBUG_FILE_INPUT
__AFL_FUZZ_INIT(); 
#endif
int main(int argc, char **argv) {

    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef DEBUG_FILE_INPUT
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <png_file>\n", argv[0]);
        return 1;
    }
    unsigned char *buf;
    int len;
    load_file_to_buf(argv[1], &buf, &len);
#else
    __AFL_INIT(); /* deferred fork server */
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
#endif
#ifndef DEBUG_FILE_INPUT
    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
#endif
        png_uint_32 height = 0; // accessed in cleanup
        png_uint_32 width = 0; 
        png_bytep* row_pointers = NULL; // accessed in cleanup
        png_struct* png = NULL;
        png_info* info = NULL;

        /* Initialize struct */ 
        png = png_create_read_struct(PNG_LIB_STR, NULL, NULL, NULL);
        if (!png) continue;
        info = png_create_info_struct(png);
        if (!info) goto cleanup;

        if (setjmp(png_jmpbuf(png))){
            // Handle all libpng longjmp
            goto cleanup;
        }

        /* set up input source */
        // We need to read from buf (i.e. __AFL_FUZZ_TESTCASE_BUF).
        // io_ptr:
        // read_data_fn: libpng calls this function when it reads png data. Copy the data from our testcase buf to libpng buffer.
        struct png_read_handle read_handle = {
            .data = buf,
            .size = (png_size_t)len,
            .offset = 0
        };
        png_set_read_fn(png, &read_handle, png_read_callback_fn);

        /* read header and metadata chunks */
        png_read_info(png, info);

        /* resource limit */
        width = png_get_image_width(png, info);
        height = png_get_image_height(png, info);
        if ((width == 0) || (height == 0) || (width > PNG_MAX_WIDTH) || (height > PNG_MAX_HEIGHT) || (width * height > PNG_MAX_PIXELS)){
            goto cleanup;
        }
        
        test_cve_2015_8472(png, info);
        // debug output
        // png_byte bit_depth = png_get_bit_depth(png, info);
        // printf("[*] bit depth = %d\n", bit_depth);
        // png_colorp palette = NULL;
        // int num_palette = 0;
        // int ret = png_get_PLTE(png, info, &palette, &num_palette);
        // if (ret & PNG_INFO_PLTE) {
        //     printf("[*] num_palette = %d\n", num_palette);
        // } else {
        //     printf("[-] missing PLTE\n");
        // }
        
        /* optionally apply transformations */
        // png_set_expand(png); /* palette -> RGB */
        // png_set_strip_16(png); /* 16-bit -> 8-bit */
        // png_set_gray_to_rgb(png); /* grayscale -> RGB */
        png_read_update_info(png, info);

        /* read pixel data */
        // WARN: Call png_read_update_info(png, info) before png_get_rowbytes if any transformation is performed
        size_t rowbytes = png_get_rowbytes(png, info);
        row_pointers = png_malloc(png, height*(sizeof (png_bytep))); // It triggers longjmp if fails
        for (int i = 0; i < height; i++) row_pointers[i] = NULL;  
        for (png_size_t row = 0; row < height; row++)
        {
            row_pointers[row] = png_malloc(png, rowbytes);
        }

        png_read_image(png, row_pointers); // read the png into row_pointers
        png_read_end(png, NULL);

        // print_image_bytes(row_pointers, height, rowbytes);
        
    cleanup:
        if (row_pointers != NULL){
            for (int i = 0; i < height; i++) {
                if (row_pointers[i] != NULL) {
                    png_free(png, row_pointers[i]);
                }
            }
            png_free(png, row_pointers);
        }
        
        png_destroy_read_struct(&png, &info, NULL);
#ifdef DEBUG_FILE_INPUT
        free(buf);
#endif

#ifndef DEBUG_FILE_INPUT
    }
#endif
    return 0;
}