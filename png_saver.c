#include "png_saver.h"
#include <stdlib.h>
#include <string.h>
#include <png.h>


static int _g_num_files_written = 0;



void save_as_png(xcb_image_t *img) {
    char filename[64];
    memset(filename, 0, sizeof(filename));
    snprintf(filename, sizeof(filename)-1, "screen_%08d.png", _g_num_files_written);

    FILE *fp = fopen(filename, "wb");
    if (!fp) return;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    png_init_io(png_ptr, fp);

    // Output is 8bit/channel depth, RGBA format.
    png_set_IHDR(png_ptr, info_ptr,
        img->width, img->height,
        8, // depth
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png_ptr, info_ptr);

    // To remove the alpha channel for PNG_COLOR_TYPE_RGB format,
    // Use png_set_filler().
    png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
    //png_set_swap_alpha(png_ptr); // RGBA > ARGB if needed
    //png_set_bgr(png_ptr); // RGB > BGR if needed


    // write image data
    png_byte **row_pointers = NULL;  // png_byte *row_pointers[height]

    int i;
    uint8_t *imgdata_ptr = img->data;

    row_pointers = (png_byte **)malloc(sizeof(png_byte *) * img->height);
    if (row_pointers) {
        for (i=0; i<img->height; i++) {
            //row_pointers[i] = (png_byte *)malloc(img->stride); // stride = Bytes per image row.
            //if (row_pointers[i]) {
            //    row_pointers[i] = imgdata_ptr;
            //}

            // directly point each row into XImage data
            row_pointers[i] = imgdata_ptr;
            imgdata_ptr += img->stride;  // move to next row
        }

        png_write_image(png_ptr, row_pointers);
        free(row_pointers);
    }
    png_write_end(png_ptr, info_ptr);


    if (info_ptr) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if (png_ptr) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    fclose(fp);
    _g_num_files_written++;
}
