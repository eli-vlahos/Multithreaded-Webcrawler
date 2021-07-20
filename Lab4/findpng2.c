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
#include <sys/wait.h>
#include <time.h>
#include <sys/time.h>

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
    int t; // number of threads
    pthread_t *tids; // tids
};

node *frontier_list_head;
node *png_list_head;
node *visited_list_head;
int frontier_size;
int m;
int pngs_found;
char *seed_url;
pthread_mutex_t visited_mutex;
pthread_mutex_t frontier_mutex;
pthread_mutex_t png_mutex;
pthread_mutex_t curl_mutex;

sem_t visited_sem;
sem_t frontier_sem;
sem_t png_sem;
sem_t curl_sem;

double times[2];
struct timeval tv;

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
int is_png(unsigned char *buf);
void free_list(node* head);
int visited_search(char *url);
void visited_add_URL(char *url);

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        // fprintf(stderr, "Document not parsed successfully.\n");
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
        // printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        // printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        // printf("No result\n");
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
                if (!visited_search(href)) {
                    // sem_wait(&frontier_sem);
                    pthread_mutex_lock( &frontier_mutex );
                    frontier_add_URL(href);
                    pthread_mutex_unlock( &frontier_mutex );
                    sem_post( &frontier_sem);
                
                }
                // sem_wait(&visited_sem);
                pthread_mutex_lock( &visited_mutex );
                visited_add_URL (href);
                pthread_mutex_unlock( &visited_mutex );
                // sem_post(&visited_sem);
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

    // sem_wait(&curl_sem);
    pthread_mutex_lock( &curl_mutex );
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    pthread_mutex_unlock( &curl_mutex );
    // sem_post(&curl_sem);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 
    
    return 0; 
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    // pid_t pid =getpid();
    // char fname[256];
    char *eurl = NULL;          /* effective URL */
    pthread_mutex_lock( &curl_mutex );
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    pthread_mutex_unlock( &curl_mutex );
    if ( eurl != NULL && is_png(p_recv_buf->buf) ) {
        // sem_wait(&png_sem);
        pthread_mutex_lock( &png_mutex );
        if (pngs_found < m && frontier_size > 0) {
            png_add_URL(eurl);
        }
        pthread_mutex_unlock( &png_mutex );
        // sem_post(&png_sem);
    }
    return 0;
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

    if ( response_code >= 400 ) { 
        return 1;
    }

    char *ct = NULL;
    // sem_wait(&curl_sem);
    pthread_mutex_lock( &curl_mutex );
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    pthread_mutex_unlock( &curl_mutex );
    // sem_post(&curl_sem);
    if ( res == CURLE_OK && ct != NULL ) {
    	// printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        //fprintf(stderr, "Failed obtain Content-Type\n");
        return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf);
    } else {
        sprintf(fname, "./output_%d", pid);
    }

    return 0;
}

// function for thread
void *do_work(void *arg) {

    // initializes struct
    struct thread_args *p_in = (struct thread_args *)arg;

    int m = p_in->m;
    int t = p_in->t;
    // pthread_t *tids = p_in->tids;
    char url[256];
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    int using_seed_url = 1;

    while ((frontier_size > 0 || using_seed_url) && pngs_found < m) {
        sem_wait( &frontier_sem );

        pthread_mutex_lock( &frontier_mutex );
        char *temp_url = frontier_take_next_url();
        strcpy(url, temp_url);
        free(temp_url);
        if (strcmp(url, seed_url) == 0) {
            using_seed_url = 1;
        } else {
            using_seed_url = 0;
        }
        pthread_mutex_unlock( &frontier_mutex );

        curl_handle = easy_handle_init(&recv_buf, url);
        if ( curl_handle == NULL ) {
            curl_global_cleanup();
            abort();
        }

        /* get it! */
        res = curl_easy_perform(curl_handle);

        while (res != CURLE_OK && frontier_size > 0) {
            cleanup(curl_handle, &recv_buf);
            pthread_mutex_lock( &frontier_mutex );

            char *temp_url = frontier_take_next_url();
            strcpy(url, temp_url);
            free(temp_url);

            pthread_mutex_unlock( &frontier_mutex );
            curl_handle = easy_handle_init(&recv_buf, url);
            res = curl_easy_perform(curl_handle);
        }
        // sem_wait(&visited_sem);
        pthread_mutex_lock( &visited_mutex );
        visited_add_URL(url);
        pthread_mutex_unlock( &visited_mutex );
        // sem_post(&visited_sem);

        /* process the download data */
        process_data(curl_handle, &recv_buf);

        /* cleaning up */
        cleanup(curl_handle, &recv_buf);

    }

    printf("hello\n");
    return NULL;
}

int main( int argc, char** argv ) 
{
    frontier_list_head = NULL;
    png_list_head = NULL;
    visited_list_head = NULL;
    frontier_size = 0;
    pngs_found = 0;

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    pthread_mutex_init( &visited_mutex, NULL );
    pthread_mutex_init( &frontier_mutex, NULL );
    pthread_mutex_init( &png_mutex, NULL );
    pthread_mutex_init( &curl_mutex, NULL );
    sem_init( &png_sem, 0 , 1 );
    sem_init(&frontier_sem, 0, 1);
    sem_init(&visited_sem, 0, 1);
    sem_init(&curl_sem, 0, 1);

    int c;
    int t;
    char *v = NULL;
    int log_lines = 15;
    char *str = "option requires an argument";
    
    while ((c = getopt (argc, argv, "t:m:v")) != -1) {
        switch (c) {
        case 't':
	        t = strtoul(optarg, NULL, 10);
	        if (t <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'm':
            m = strtoul(optarg, NULL, 10);
            if (m <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 'n'\n", argv[0], str);
                return -1;
            }
            break;
        case 'v':
            v = argv[argc-2];
            if (v[0] == m){
                v = NULL;
            }
            break;
        }
    }

    seed_url = argv[argc-1];

    curl_global_init(CURL_GLOBAL_DEFAULT);

    frontier_add_URL(seed_url);

    // threads are initialized
    pthread_t *p_tids = malloc(sizeof(pthread_t) * t);
    struct thread_args in_params[t];

    // creates appropriate threads
    in_params->tids = p_tids;
    for (int i=0; i<t; i++) {
        in_params[i].m = m;
        in_params[i].t = t;
        // in_params[i].tids = p_tids[i];
        // calls thread function
        pthread_create(p_tids + i, NULL, do_work, in_params + i); 
    }

    for(int i = 0; i< t; i++) {
        pthread_join(p_tids[i], NULL);
    }

    if (v != NULL){

        FILE *fp = NULL;
        fp = fopen(v, "w");

        while(visited_list_head != NULL){
            if (visited_list_head == NULL){
                break;
            }
            fprintf(fp, visited_list_head->url);
            fprintf(fp, "\n");
            
            // do if statement
            node *temp = visited_list_head;
            visited_list_head = visited_list_head->next;
            free(temp->url);
            free(temp);
        }
        fclose(fp);
    }

    FILE *png_file = NULL;
    png_file = fopen("png_urls.txt", "w");

    while(png_list_head != NULL){
        fprintf(png_file, png_list_head->url);
        fprintf(png_file, "\n");
        
        node *temp = png_list_head;
        png_list_head = png_list_head->next;
        free(temp->url);
        free(temp);
    }
    fclose(png_file);

    free_list(frontier_list_head);

    // frees dynamically allocated threads
    free(p_tids);
    curl_global_cleanup();

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %.6lf seconds\n",  times[1] - times[0]);

    sem_destroy( &png_sem );
    sem_destroy( &frontier_sem);
    sem_destroy( &visited_sem);
    sem_destroy( &curl_sem);
    pthread_mutex_destroy( &visited_mutex );
    pthread_mutex_destroy( &frontier_mutex );
    pthread_mutex_destroy( &png_mutex );
    pthread_mutex_destroy( &curl_mutex );

    pthread_exit( 0 );

    return 0;
}


void frontier_add_URL(char *url) {
    node *n = malloc(sizeof(node));
    node *current = frontier_list_head;
    char *val = malloc((strlen(url)+1)*1);
    strcpy(val,url);
    n->url = val;
    n->next = NULL;

    if (frontier_list_head == NULL){
        frontier_list_head = n;
    } else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = n;
    }
    frontier_size += 1;
}
 

char *frontier_take_next_url() {
    if (frontier_list_head == NULL)
    {
        /* Something has gone wrong */
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
    } else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = n;
    }
    pngs_found += 1;
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


void free_list(node* head) {

   while (head != NULL) {
        node* tmp = head;
        head = head->next;

        free(tmp->url);
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
