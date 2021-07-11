/* necessary imports */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lab_png.h"  /* simple PNG data structures  */
#include "crc.h"  /* simple PNG data structures  */
#include "zutil.h"
#include "./write_header_cb.h"
#include <pthread.h>
#include <unistd.h>
#include <curl/curl.h>
#include <getopt.h>

void write_chunk_to_file(struct chunk *curr_chunk);
void catpng (struct recv_buf2 * recv_data);

struct thread_args              /* thread input parameters struct */
{
    /* different inputs for thread */

    int *counter; // pointer to counter, shared across all threads
    struct recv_buf2 *png_array; // data retrieved
    int image_option; // image that will be retrieved
    int server; //server that is going to be used for thread
};

// function for thread
void *do_work(void *arg) {

    // initializes struct
    struct thread_args *p_in = (struct thread_args *)arg;

    // printf("COUNTER: %i\n", *p_in->counter);
    // printf("IMG OPT: %i\n", p_in->image_option);

    while((*p_in->counter) < 50) {

        // fetches struct data
        struct recv_buf2 temp = fetch(p_in->image_option, p_in->server);

        // printf("SEQ: %i\n", temp.seq);
        // checks to see if image part has already been retrieved
        if (temp.seq != -1 && p_in->png_array[temp.seq].max_size == 0) {
            
            memcpy(&(p_in->png_array[temp.seq]), &temp, sizeof(temp));

            (*p_in->counter)++;

        } // if there is an error, clean up temp
        else if(temp.seq == -1) {
            (*p_in->counter) = 50;
            recv_buf_cleanup(&temp);
        } // else clean temp memory
        else {
            recv_buf_cleanup(&temp);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {

    // code used to get command line input, all copied from starter file main_getopt.c
    int c;
    int t = 1;
    int n = 1;
    char *str = "option requires an argument";
    
    while ((c = getopt (argc, argv, "t:n:")) != -1) {
        switch (c) {
        case 't':
	    t = strtoul(optarg, NULL, 10);
	    if (t <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'n':
            n = strtoul(optarg, NULL, 10);
            if (n <= 0 || n > 3) {
                fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
                return -1;
            }
            break;
        default:
            return -1;
        }
    }

    // ends here

    // initializes curl information
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // struct for data, variable for counter and image
    struct recv_buf2 png_array[50] = {0};
    int counter = 0;
    int image_option = n;
    // threads are initialized
    pthread_t *p_tids = malloc(sizeof(pthread_t) * t);
    struct thread_args in_params[t];

    // creates appropriate threads
    for (int i=0; i<t; i++) {
        in_params[i].counter = &counter;
        in_params[i].image_option = image_option;
        in_params[i].png_array = &png_array[0];
        in_params[i].server = i%3;

        if (in_params[i].server == 0){
            in_params[i].server = 3;
        }
        // calls thread function
        pthread_create(p_tids + i, NULL, do_work, in_params + i); 
    }

    for(int i = 0; i< t; i++) {
        pthread_join(p_tids[i], NULL);
        //fprintf(stderr, "Thread %d terminated\n", i);
    }
    
    // calls catpng, combining all data together
    catpng(&png_array[0]);

    // frees dynamically allocated threads
    free(p_tids);
    curl_global_cleanup();
    
    //cleans up array
    for (int i = 0; i < 50; i++){
        recv_buf_cleanup(&png_array[i]);
    }

    return 0;
}

void catpng (struct recv_buf2 * recv_data) {
    
    U32 height_all = 0;
    U64 length_all = 0;
    U32 width = 0;

    struct chunk *ihdr_all;
    struct chunk *iend_all;
    struct chunk *idat_all = malloc(sizeof(struct chunk));

    /* total (all.png) height and width (width is same for all PNGs) */
    height_all = 300;
    width = 400;

    /* setting array for IDAT chunk data, using size of given formula for uncompressed data 
        counter will keep track of size after each PNGs data, also keeps track of index in array */
    U8 IDAT_data[height_all*(width*4+1)];
    int counter = 0;

    for (int i=1; i < 50; i++) { /* go through all inputted png files to get IDAT data chunks */


        struct recv_buf2 file_array = recv_data[i];

        /* initializes chunks for idat and place for uncompressed data */
        struct chunk * idat;

        U32 height = 6;
        U64 uncompressed_size = height*(width*4+1);
        U8 temp_data[uncompressed_size];

        /* using our helper function to get chunk information */
        idat = retrieve_chunk(&file_array.buf[33]);

        /* variables that only need to be set once, doesn't matter which inputted PNG it uses */
        if (i==1) {
            ihdr_all = retrieve_chunk(&file_array.buf[8]);
            iend_all = retrieve_chunk(&file_array.buf[recv_data[i].size-12]);
        }

        /* update sum values for lengths and uncompress the file */
        length_all += idat->length;
        mem_inf(&temp_data[0], &uncompressed_size, idat->p_data, idat->length);

        /* uncompressed data goes into IDAT_data array */
        memcpy(&IDAT_data[counter], &temp_data[0], uncompressed_size);

        /* update the counter to keep track of which value the array is at */
        counter += uncompressed_size;

        /* free dynamically allocated space */
        free (idat->p_data);
        free (idat);
    }

    /* initialize array that stores all IDAT data values (compressed) */
    U8 IDAT_data_all[length_all]; /* length of all.png IDAT chunk data (compressed) */

    printf("%u, %u \n", counter, length_all);

    /* compress data */
    mem_def(&IDAT_data_all[0], &length_all, IDAT_data, counter, -1); /* compressing data of all PNG before it goes in IDAT chunk */

    /* initializes length of complete idat and data */
    idat_all->length = length_all;
    idat_all->p_data = &IDAT_data_all[0];

    /* initializes type values in a character array (for crc use and to initialize type for IDAT chunk) */
    U8 idat_type[4] = {73,68,65,84};
    U8 ihdr_type[4] = {73,72,68,82};

    memcpy(&idat_all->type[0], &idat_type[0], 4);

    /* initializes arrays for crc check */
    unsigned char ihdr_buf[DATA_IHDR_SIZE+CHUNK_TYPE_SIZE];
    unsigned char idat_buf[length_all+CHUNK_TYPE_SIZE];

    /* copy a combination of data and type to new character array to check crc values */
    memcpy(&idat_buf[0], &idat_type[0], CHUNK_TYPE_SIZE);
    memcpy(&idat_buf[4], &IDAT_data_all[0], length_all);

    /* initialize the crc values for the new idat */
    U32 crc_idat = crc(&idat_buf[0], length_all+CHUNK_TYPE_SIZE);

    /* update length and crc values in chunks with reversed values */
    idat_all->crc = ntohl(crc_idat);

    U32 iend_crc_temp = ntohl(iend_all->crc);
    iend_all->crc = iend_crc_temp;

    /* create array to store values for IHDR data */
    U8 ihdr_data_all[DATA_IHDR_SIZE];

    height_all = ntohl(height_all);
    width = ntohl(width);

    for (int i = 0; i < 4; i++){
        /* calculates height and width and updates IHDR chunk data */
        ihdr_data_all[3-i] = width/((int)pow(256, 3 - i));
        ihdr_data_all[7-i] = height_all/((int)pow(256, 3 - i));
        height_all = height_all % ((int)pow(256, 3 - i));
        width = width % ((int)pow(256, 3 - i));
    }
    /* rest of IHDR data (fixed values) */
    ihdr_data_all[8] = 8;
    ihdr_data_all[9] = 6;
    ihdr_data_all[10] = 0;
    ihdr_data_all[11] = 0;
    ihdr_data_all[12] = 0;

    free(ihdr_all->p_data);
    ihdr_all->p_data = &ihdr_data_all[0]; /* updating IHDR data (new height and width) */

    /* update crc value in IHDR chunk with reversed values */
    memcpy(&ihdr_buf[0], &ihdr_type[0], 4);
    memcpy(&ihdr_buf[4], &ihdr_data_all[0], 13);
    U32 crc_ihdr = crc(ihdr_buf, DATA_IHDR_SIZE+CHUNK_TYPE_SIZE);
    ihdr_all->crc = ntohl(crc_ihdr);

    /* write 8 byte png header */
    FILE *all_png;
    all_png = fopen("all.png", "wb");
    U8 eight_byte_hdr[PNG_SIG_SIZE] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(&eight_byte_hdr, sizeof(U8), PNG_SIG_SIZE, all_png);
    fclose(all_png);

    /* writes each chunk to our file */
    write_chunk_to_file(ihdr_all);
    write_chunk_to_file(idat_all);
    write_chunk_to_file(iend_all);

    /* frees chunks */
    free (iend_all->p_data);
    free (ihdr_all);
    free (idat_all);
    free (iend_all);
}


void write_chunk_to_file(struct chunk *curr_chunk) {
    FILE *fp;
    fp = fopen("all.png", "ab");

    U32 length = (ntohl(curr_chunk->length));
    U32 crc = (curr_chunk->crc);

    /* writes each chunk attribute to file */
    fwrite(&length, sizeof(U32), 1, fp);
    fwrite(&curr_chunk->type, sizeof(U8), 4, fp);
    fwrite(&curr_chunk->p_data[0], sizeof(U8), curr_chunk->length, fp);
    fwrite(&crc, sizeof(U32), 1, fp);

    fclose(fp);
}

