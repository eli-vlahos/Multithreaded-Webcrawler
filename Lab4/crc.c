/********************************************************************
 * @file: crc.c
 * @brief: PNG crc calculation
 * Reference: https://www.w3.org/TR/PNG-CRCAppendix.html
 */

/* Table of CRCs of all 8-bit messages. */

#include "crc.h"
unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computed = 0;

/* Make the table for a fast CRC. */
void make_crc_table(void)
{
    unsigned long c;
    int n, k;

    for (n = 0; n < 256; n++) {
        c = (unsigned long) n;
        for (k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
   should be initialized to all 1's, and the transmitted value
   is the 1's complement of the final running CRC (see the
   crc() routine below)). */

unsigned long update_crc(unsigned long crc, unsigned char *buf, int len)
{
    unsigned long c = crc;
    int n;

    if (!crc_table_computed)
        make_crc_table();
    for (n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
unsigned long crc(unsigned char *buf, int len)
{
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

/* function that returns information for chunk
 takes in character array for png binary file as input */
struct chunk * retrieve_chunk(char *fp){

    /*dyamically allocate chunk information */
    struct chunk * c = malloc(sizeof(struct chunk));

    /*initialize two arrays for crc and length bytes, used to combine the bytes later */
    unsigned char length[4] = {0};
    unsigned char crc_array[4] = {0};

    /* copy bytes for length and type into */
    memcpy(length, &fp[0], 4);

    memcpy(c->type, &fp[4], 4);

    /* initialize length */
    c->length = 0;
    for(int i = 0; i < 4; i++){
        unsigned int sum = 0;
        sum = length[i]*((int)pow(256, i));
        c->length = c->length + sum;
    }

    /* length in correct order (little endian) */
    c->length = ntohl(c->length);

    /* copy type, data and crc values into struct chunk */
    c->p_data = (U8 *)malloc(c->length*sizeof(U8));

    memcpy(c->p_data, &fp[8], c->length);

    memcpy(crc_array, &fp[8+c->length], 4);

    c->crc = 0;
    for(int i = 0; i < 4; i++){
        unsigned int sum = 0;
        sum = crc_array[i]*((int)pow(256, i));
        c->crc = c->crc + sum;
    }

    c->crc = ntohl(c->crc);
    
    return c;
}