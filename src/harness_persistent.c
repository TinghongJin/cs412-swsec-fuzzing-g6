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


#define PNG_LIB_STR PNG_LIBPNG_VER_STRING

// We limit width x height so that we have better flexibililty
#define PNG_MAX_PIXELS 262144
// Alternatives:
// #define PNG_MAX_WIDTH 512
// #define PNG_MAX_HEIGHT 512
// Custom png read handler.
struct png_read_handle {
    const png_byte* data;
    const png_size_t size;
    png_size_t offset;
};

// The callback function provided to png_set_read_fn(). Copies data from afl buffer to *data. Updates the read handler.
void png_read_callback_fn(png_struct *png, png_byte *out_data, png_size_t length)
{
    struct png_read_handle* handle = (struct png_read_handle*)png_get_io_ptr(png);
    if (handle->offset + length > handle->size) png_error(png, "read past end of input");
    memcpy(out_data, handle->data + handle->offset, length);
    handle->offset += length;
}

__AFL_FUZZ_INIT(); 
int main(int argc, char **argv) {

    __AFL_INIT(); /* deferred fork server */
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {

        int len = __AFL_FUZZ_TESTCASE_LEN;
        /* feed buf/len to libpng, clean up state */

        png_struct* png = png_create_read_struct(PNG_LIB_STR, NULL, NULL, NULL);
        png_info* info = png_create_info_struct(png);
        png_bytep *row_pointers = NULL;

        int row_ptrs_allocated = 0;

        // png_uint_32 height = 0;

        /* Add setjmp */
        if (setjmp(png_jmpbuf(png))) {
            // fprintf(stderr, "[DEBUG] setjmp triggered! libpng aborted parsing.\n");
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
        png_uint_32 width = png_get_image_width(png, info);
        png_uint_32 height = png_get_image_height(png, info);
        // fprintf(stderr, "[DEBUG] Header parsed: Width = %u, Height = %u\n", width, height);
        if ((width == 0) || (height == 0) || (width * height > PNG_MAX_PIXELS)){
            // fprintf(stderr, "[DEBUG] Image dimensions out of bounds, exiting.\n");
            goto cleanup;
        }
        

        /* optionally apply transformations */
        png_set_expand(png); /* palette -> RGB */
        png_set_strip_16(png); /* 16-bit -> 8-bit */
        png_set_gray_to_rgb(png); /* grayscale -> RGB */
        png_read_update_info(png, info);



        /* read pixel data */
        // WARN: Call png_read_update_info(png, info) before png_get_rowbytes if any transformation is performed
        size_t rowbytes = png_get_rowbytes(png, info);
        // fprintf(stderr, "[DEBUG] Transforms applied: Rowbytes = %zu\n", rowbytes);
        // row_pointers = png_malloc(png, height*(sizeof (png_bytep)));
        row_pointers = png_malloc(png, height * sizeof(png_bytep));
        for (int i = 0; i < height; i++) row_pointers[i] = NULL;  
        for (png_size_t row = 0; row < height; row++)
        {
            row_pointers[row] = png_malloc(png, rowbytes);
        }
        png_read_image(png, row_pointers); // read the png into row_pointers
        

        /* DEBUG output
        for (png_uint_32 i = 0; i < height; i++) {
            if (row_pointers[i] != NULL) {
                for (size_t j = 0; j < rowbytes; j++) {
                    printf("%02X ", row_pointers[i][j]); 
                }
            }
            printf("\n");
        }*/

        /* read post-IDAT chunks */
        png_read_end(png, NULL);
        // fprintf(stderr, "[DEBUG] SUCCESS! Image fully decoded and rows read.\n");

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
        
    }
    return 0;
}
