#include "lab_png.h"
/* include appropriate header file */

/* gets an element of type data_IHDR, using a character array as an arument */
struct data_IHDR * get_png_data_IHDR(unsigned char *p_data){

    /* initialize IHDR that will be returned */
    struct data_IHDR *chunk = malloc(sizeof(struct data_IHDR));

    /* initialize height and width integers */
    unsigned int width = 0;
    unsigned int height = 0;

    /* runs through each element and appriopriately appends values to height and width */
    for (int i = 0; i < 4;i++){
        unsigned int sum_1 = 0;
        unsigned int sum_2 = 0;

        sum_1 = p_data[i]*((int)pow(256, i));
        sum_2 = p_data[i + 4]*((int)pow(256, i));

        width = width + sum_1;
        height = height + sum_2;
    }

    /* reverse byte order (big to small endian) */
    width = ntohl(width);
    height = ntohl(height);

    /* initialize all values into chunk and return it */
    chunk->width = width;
    chunk->height = height;
    chunk->bit_depth = p_data[9];
    chunk->color_type = p_data[10];
    chunk->filter = p_data[11];
    chunk->interlace = p_data[12];

    return chunk;
}

/* checks to see if it is png file, input is character array */
int is_png(U8 *buf) {

    /* goes through each element and cross references it with a list of bytes that represent png */
    int is_PNG = 1;
    unsigned char PNG_magic_number_decimal[PNG_SIG_SIZE] = {137, 80, 78, 71, 13, 10, 26, 10};

    for (int i=0; i<PNG_SIG_SIZE; i++) {
        if (buf[i] != PNG_magic_number_decimal[i]) {
            is_PNG = 0;
        }
    }
    return is_PNG;
}