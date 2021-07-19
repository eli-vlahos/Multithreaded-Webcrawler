/*
 * The code is derived from cURL example and paster.c base code.
 * The cURL example is at URL:
 * https://curl.haxx.se/libcurl/c/getinmemory.html
 * Copyright (C) 1998 - 2018, Daniel Stenberg, <daniel@haxx.se>, et al..
 *
 * The xml example code is 
 * http://www.xmlsoft.org/tutorial/ape.html
 *
 * The paster.c code is 
 * Copyright 2013 Patrick Lam, <p23lam@uwaterloo.ca>.
 *
 * Modifications to the code are
 * Copyright 2018-2019, Yiqing Huang, <yqhuang@uwaterloo.ca>.
 * 
 * This software may be freely redistributed under the terms of the X11 license.
 */

/** 
 * @file main_wirte_read_cb.c
 * @brief cURL write call back to save received data in a user defined memory first
 *        and then write the data to a file for verification purpose.
 *        cURL header call back extracts data sequence number from header if there is a sequence number.
 * @see https://curl.haxx.se/libcurl/c/getinmemory.html
 * @see https://curl.haxx.se/libcurl/using/
 * @see https://ec.haxx.se/callback-write.html
 */ 


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <search.h>
#include <pthread.h>
#include <semaphore.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <time.h>

#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>


#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct node {
    char *url;
    struct node *next;
} node;

struct thread_args              /* thread input parameters struct */
{
    /* different inputs for thread */

    int m; // number of pngs to get
};

node *frontier_list_head;
node *png_list_head;
node *visited_list_head;
int frontier_size;
// int png_size;
int pngs_found;
pthread_mutex_t mutex;
sem_t available;

#define PNG_SIG_SIZE    8 /* number of bytes of png image signature data */

htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath);
int find_http(char *fname, int size, int follow_relative_links, const char *base_url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);
void frontier_add_URL(char *p_recv_buf);
char *frontier_take_next_url();
void png_add_URL(char *p_recv_buf);
char *png_take_next_url();
void printList(node* head);
int is_png(unsigned char *buf);
void free_list(node* head);
int visited_search(char *url);

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        printf("No result\n");
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		
    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                // printf("href: %s\n", href);
                // char *url;
                // sprintf(url, "%s", href);
                // printf("url!123: %s\n", href);
                if (!visited_search(href)) {
                    frontier_add_URL(href);
                }
                //printList(frontier_list_head);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}
/**
 * @brief  cURL header call back function to extract image sequence number from 
 *         http header data. An example header for image part n (assume n = 2) is:
 *         X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the full
 * header data are received.  we are only interested in the ECE252_HEADER line 
 * received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

#ifdef DEBUG1_
    // printf("%s", p_recv);
#endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

        /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}


/**
 * @brief write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv, 
 *        which is provided by libcurl and is not user allocated memory.
 *        The user allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function maybe invoked more than once by one invokation of
 *        curl_easy_perform().
 */

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}


int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

void cleanup(CURL *curl, RECV_BUF *ptr)
{
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        recv_buf_cleanup(ptr);
}


/**
 * @brief create a curl easy handle and set the options.
 * @param RECV_BUF *ptr points to user data needed by the curl write call back function
 * @param const char *url is the target url to fetch resoruce
 * @return a valid CURL * handle upon sucess; NULL otherwise
 * Note: the caller is responsbile for cleaning the returned curl handle
 */

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        // fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char fname[256];
    int follow_relative_link = 1;
    char *url = NULL; 
    pid_t pid =getpid();

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    // node *curr_url_frontier = frontier_list_head;
    // check that recv_buf seq # isn't in hash table already
    // ENTRY url_info;
    // url_info.key = strdup(url);
    // url_info.data = NULL;
    
    // printf("hsearch returns: %i\n", hsearch(url_info, FIND));

    // if (hsearch(url_info, FIND) == NULL) {
    //     printf("haven't seen this url before!\n");
    //     // add to visited
    //     // printf("URL WE aDD: %s\n", url_info.key);
    //     // hsearch(url_info, ENTER);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 
    // } else {
    //     printf("already visited this url\n");
    // }
    
    return 0; //write_file(fname, p_recv_buf->buf, p_recv_buf->size);
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    pid_t pid =getpid();
    char fname[256];
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if ( eurl != NULL && is_png(p_recv_buf->buf)) {
        printf("The PNG url is: %s\n", eurl);
        // ENTRY url_info;
        // url_info.key = strdup(eurl);
        // url_info.data = NULL;
        // printf("hsearch returns: %i\n", hsearch(url_info, FIND));

        // if (!visited_search(eurl)) {
        png_add_URL(eurl);
        printf("pngs_found: %i\n", pngs_found);
        // }
    }
    // sprintf(fname, "./output_%d_%d.png", p_recv_buf->seq, pid);
    return 0; //write_file(fname, p_recv_buf->buf, p_recv_buf->size);
}
/**
 * @brief process teh download data by curl
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data. 
 * @return 0 on success; non-zero otherwise
 */

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    CURLcode res;
    char fname[256];
    pid_t pid =getpid();
    long response_code;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if ( res == CURLE_OK ) {
	    // printf("Response code: %ld\n", response_code);
    }

    if ( response_code >= 400 ) { 
    	fprintf(stderr, "Error.\n");
        return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    	// printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        fprintf(stderr, "Failed obtain Content-Type\n");
        return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf);
    } else {
        sprintf(fname, "./output_%d", pid);
    }

    return 0;//write_file(fname, p_recv_buf->buf, p_recv_buf->size);
}

// function for thread
void *do_work(void *arg) {

    // initializes struct
    struct thread_args *p_in = (struct thread_args *)arg;

    int m = p_in->m;
    char url[256];
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    while (frontier_size > 0 && pngs_found < m) {
        // printf("frontier size: %i\n", frontier_size);

        sem_wait( &available );
        pthread_mutex_lock( &mutex );

        strcpy(url, frontier_take_next_url());
        // printf("URL: %s\n", url);
        // printList(visited_list_head);

        curl_handle = easy_handle_init(&recv_buf, url);
        if ( curl_handle == NULL ) {
            fprintf(stderr, "Curl initialization failed. Exiting...\n");
            curl_global_cleanup();
            abort();
        }
        /* get it! */
        res = curl_easy_perform(curl_handle);

        while (res != CURLE_OK && frontier_size > 0) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            cleanup(curl_handle, &recv_buf);
            strcpy(url, frontier_take_next_url());
            printf("taking next URL: %s\n", url);
            curl_handle = easy_handle_init(&recv_buf, url);
            ENTRY url_info;
            url_info.key = url;
            url_info.data = NULL;
            (void) hsearch(url_info, ENTER);
            res = curl_easy_perform(curl_handle);
        }


        if (!visited_search(url)) {
            visited_add_URL(url);

            /* process the download data */
            process_data(curl_handle, &recv_buf);
            printf("frontier size: %i\n", frontier_size);

        }
        
        /* cleaning up */
        if (frontier_size == 0 || pngs_found == m) {
            cleanup(curl_handle, &recv_buf);
        }

        pthread_mutex_unlock( &mutex );
        sem_post( &available );

    }

    return NULL;
}

int main( int argc, char** argv ) 
{
    frontier_list_head = NULL;
    png_list_head = NULL;
    visited_list_head = NULL;
    frontier_size = 0;
    pngs_found = 0;

    double times[2];
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    sem_init( &available, 0, 1 );
    pthread_mutex_init( &mutex, NULL );

    // visited hash table
    (void) hcreate(10000);
    int use_seed_url = 1;

    int c;
    int t = 5;
    int m = 13;
    char *v = NULL;
    int log_lines = 15;
    char *str = "option requires an argument";
    
    // while ((c = getopt (argc, argv, "t:m:v")) != -1) {
    //     switch (c) {
    //     case 't':
	//         t = strtoul(optarg, NULL, 10);
	//         printf("option -t specifies a value of %d.\n", t);
	//         if (t <= 0) {
    //             fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
    //             return -1;
    //         }
    //         break;
    //     case 'm':
    //         m = strtoul(optarg, NULL, 10);
	//         printf("option -m specifies a value of %d.\n", m);
    //         if (m <= 0) {
    //             fprintf(stderr, "%s: %s > 0 -- 'n'\n", argv[0], str);
    //             return -1;
    //         }
    //         break;
    //     // case 'v':
    //     //     v = optarg;
	//     //     printf("option -v specifies a value of %s.\n", *v);
    //     //     // if (v <= 0) {
    //     //     //     fprintf(stderr, "%s: %s 1, 2, or 3 -- 'v'\n", argv[0], str);
    //     //     //     return -1;
    //     //     // }
    //     //     break;
    //     default:
    //         return -1;
    //     }
    // }
    curl_global_init(CURL_GLOBAL_DEFAULT);

    frontier_add_URL(SEED_URL);
    // visited_add_URL(SEED_URL);

    // threads are initialized
    pthread_t *p_tids = malloc(sizeof(pthread_t) * t);
    struct thread_args in_params[t];

    // creates appropriate threads
    for (int i=0; i<t; i++) {
        in_params[i].m = m;

        // calls thread function
        pthread_create(p_tids + i, NULL, do_work, in_params + i); 
    }

    for(int i = 0; i< t; i++) {
        pthread_join(p_tids[i], NULL);
        //fprintf(stderr, "Thread %d terminated\n", i);
    }

    printf("PRINTING PNGS FOUND:\n");
    printList(png_list_head);

    if (log_lines != 0){

        // need to replace file name
        FILE *fp = NULL;
        fp = fopen("file.txt", "w");

        for (int i = 0; i < log_lines; i++){
            if (visited_list_head == NULL){
                break;
            }
            fprintf(fp, visited_list_head->url);
            fprintf(fp, "\n");
            
            // do if statement
            node *temp = visited_list_head;
            visited_list_head = visited_list_head->next;
            free(temp);
        }
        fclose(fp);
    }

    // need to replace file name
    FILE *png_file = NULL;
    png_file = fopen("png_urls.txt", "w");

    while(png_list_head != NULL){
        fprintf(png_file, png_list_head->url);
        fprintf(png_file, "\n");
        
        node *temp = png_list_head;
        png_list_head = png_list_head->next;
        free(temp);
    }
    fclose(png_file);

    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %u seconds", getpid(),  times[1] - times[0]);

    free_list(png_list_head);
    free_list(frontier_list_head);

    // frees dynamically allocated threads
    free(p_tids);
    curl_global_cleanup();

    sem_destroy( &available );
    pthread_mutex_destroy( &mutex );
    pthread_exit( 0 );


    return 0;
}


void frontier_add_URL(char *url) {
    // RECV_BUF *u = malloc(sizeof(RECV_BUF));
    node *n = malloc(sizeof(node));
    node *current = frontier_list_head;
    char *val = malloc((strlen(url)+1)*1);
    strcpy(val,url);
    n->url = val;
    n->next = NULL;

    if (frontier_list_head == NULL){
        frontier_list_head = n;
        // printf("added at beginning\n");
    } else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = n;
        // printf("added later\n");
    }
    frontier_size += 1;
    // printList(frontier_list_head);
}
 

char *frontier_take_next_url() {
    if (frontier_list_head == NULL)
    {
        /* Something has gone wrong */
        // printf("Trying to take from an empty list!\n");
        exit(-1);
    }
    node *head = frontier_list_head;
    char *u = head->url;
    frontier_list_head = frontier_list_head->next;
    free(head);
    frontier_size -= 1;
    return u;
}

void png_add_URL(char *url) {
    node *n = malloc(sizeof(node));
    node *current = png_list_head;
    char *val = malloc((strlen(url)+1)*1);
    strcpy(val,url);
    n->url = val;
    n->next = NULL;

    if (png_list_head == NULL){
        png_list_head = n;
        // printf("added at beginning\n");
    } else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = n;
        // printf("added later\n");
    }
    pngs_found += 1;
    // printList(png_list_head);
}

void visited_add_URL(char *url) {
    node *n = malloc(sizeof(node));
    node *current = visited_list_head;
    char *val = malloc((strlen(url)+1)*1);
    strcpy(val,url);
    n->url = val;
    n->next = NULL;

    if (visited_list_head == NULL){
        visited_list_head = n;
    } else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = n;
    }
}


int visited_search(char *url) {
    node* current = visited_list_head;

    while (current != NULL) {
        if (strcmp(current->url, url) == 0)
            return 1;
        current = current->next;
    }
    return 0;
}
 

char *png_take_next_url() {
    if (png_list_head == NULL)
    {
        /* Something has gone wrong */
        // printf("Trying to take from an empty list!\n");
        exit(-1);
    }
    node *head = png_list_head;
    char *u = head->url;
    png_list_head = png_list_head->next;
    free(head);
    // pngs_found -= 1;
    return u;
}

void free_list(node* head) {
   node* tmp;
   while (head != NULL) {
        tmp = head;
        head = head->next;
        free(tmp);
    }
}

/* checks to see if it is png file, input is character array */
int is_png(unsigned char *buf) {

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

void printList(node* head) {
    node* curr = head;
    while (curr) {
        printf("LL: %s \n", curr->url);
        curr = curr->next;
    }
    printf("null\n");
}