/*
 * Copyright (c) 2014 DeNA Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "h2o.h"

#ifndef MIN
#define MIN(a, b) (((a) > (b)) ? (b) : (a))
#endif

static h2o_mem_pool_t pool;
static int delta = 1;
static char *method = "GET";
static int body_size = 0;
static int chunk_size = 10;
static h2o_iovec_t iov_filler;
static int delay_interval_ms = 0;
static int ssl_verify_none = 1;
static int http2_ratio = 100;
static int cur_body_size;
static char *save_path = NULL;
FILE *save_file = NULL;
FILE *dat_file[3] = {0};
char dat_file_name[3][100] = {"pics/data1.txt", "pics/data2.txt", "pics/data3.txt"};
static char *server_file_path = NULL;
static char url1[2048] = {0};
static char url2[2048] = {0};
static char url3[2048] = {0};
unsigned char *file_buf = NULL;
size_t full_size = 0;
size_t chunk = 0;
u_int8_t is_first_stream = 1;
u_int8_t is_all_done;
sheduler_ctx s_ctx[3] = {0};
h2o_httpclient_ctx_t *ctx;
struct timeval file_start, file_end;
int all_done_flag = 0;


static h2o_httpclient_head_cb on_connect(h2o_httpclient_t *client, const char *errstr, h2o_iovec_t *method, h2o_url_t *url,
                                         const h2o_header_t **headers, size_t *num_headers, h2o_iovec_t *body,
                                         h2o_httpclient_proceed_req_cb *proceed_req_cb, h2o_httpclient_properties_t *props,
                                         h2o_url_t *origin);
static h2o_httpclient_body_cb on_head(h2o_httpclient_t *client, const char *errstr, int version, int status, h2o_iovec_t msg,
                                      h2o_header_t *headers, size_t num_headers, int header_requires_dup);

static void on_exit_deferred(h2o_timer_t *entry)
{
    h2o_timer_unlink(entry);
    exit(1);
}
static h2o_timer_t exit_deferred;

static void on_error(h2o_httpclient_ctx_t *ctx, const char *fmt, ...)
{
    char errbuf[2048];
    va_list args;
    va_start(args, fmt);
    int errlen = vsnprintf(errbuf, sizeof(errbuf), fmt, args);
    va_end(args);
    fprintf(stderr, "%.*s\n", errlen, errbuf);

    /* defer using zero timeout to send pending GOAWAY frame */
    memset(&exit_deferred, 0, sizeof(exit_deferred));
    exit_deferred.cb = on_exit_deferred;
    h2o_timer_link(ctx->loop, 0, &exit_deferred);
}

static void start_request(h2o_httpclient_ctx_t *ctx, char *url)
{
    all_done_flag += 1;
    printf("\033[35m[start request]\033[0m %s\n", url);
    h2o_url_t *url_parsed;
    h2o_httpclient_connection_pool_t *connpool = ctx->s_ctx->connpool;

    /* clear memory pool */
    h2o_mem_clear_pool(&pool);

    /* parse URL */
    url_parsed = h2o_mem_alloc_pool(&pool, *url_parsed, 1);
    if (h2o_url_parse(url, SIZE_MAX, url_parsed) != 0) {
        on_error(ctx, "unrecognized type of URL: %s", url);
        return;
    }

    cur_body_size = body_size;

    /* initiate the request */
    if (connpool == NULL) {
        connpool = h2o_mem_alloc(sizeof(*connpool));
        h2o_socketpool_t *sockpool = h2o_mem_alloc(sizeof(*sockpool));
        h2o_socketpool_target_t *target = h2o_socketpool_create_target(url_parsed, NULL);
        h2o_socketpool_init_specific(sockpool, 10, &target, 1, NULL);
        h2o_socketpool_set_timeout(sockpool, 5000 /* in msec */);
        h2o_socketpool_register_loop(sockpool, ctx->loop);
        h2o_httpclient_connection_pool_init(connpool, sockpool);

        /* obtain root */
        char *root, *crt_fullpath;
        if ((root = getenv("H2O_ROOT")) == NULL)
            root = H2O_TO_STR(H2O_ROOT);
#define CA_PATH "/share/h2o/ca-bundle.crt"
        crt_fullpath = h2o_mem_alloc(strlen(root) + strlen(CA_PATH) + 1);
        sprintf(crt_fullpath, "%s%s", root, CA_PATH);
#undef CA_PATH

        SSL_CTX *ssl_ctx = SSL_CTX_new(TLSv1_2_client_method());
        SSL_CTX_load_verify_locations(ssl_ctx, crt_fullpath, NULL);
        if (ssl_verify_none) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
        } else {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        }
        h2o_socketpool_set_ssl_ctx(sockpool, ssl_ctx);
        SSL_CTX_free(ssl_ctx);
        ctx->s_ctx->connpool = connpool;
    }
    h2o_httpclient_connect(NULL, &pool, url_parsed, ctx, connpool, url_parsed, on_connect);
}

static int on_body(h2o_httpclient_t *client, const char *errstr)
{
    if (errstr != NULL && errstr != h2o_httpclient_error_is_eos) {
        on_error(client->ctx, errstr);
        return -1;
    }
    algorithm_ctx *alg = client->get_alg(client);
    struct timeval now;
    gettimeofday(&now, NULL);
    double time = now.tv_sec - alg->last_body_time.tv_sec
                    + (now.tv_usec - alg->last_body_time.tv_usec) * 1.0 / 1000000;
    alg->downloaded += (*client->buf)->size;
    // printf("\033[33m[DEBUG]\033[0m [size: %ld; time: %lf]\n", (*client->buf)->size, time);
    alg->bandwidth = (*client->buf)->size / (time * 1024 * 1024);
    // printf("\033[33m[DEBUG]\033[0m [This bandwidth: %lfM/s]\n", alg->bandwidth);
    alg->last_body_time = now;
    if (alg->bw_cnt != 5) {
        alg->bandwidth_list[alg->bw_cnt++] = alg->bandwidth;
    }
    else {
        for (int i = 0; i < 4; ++i) {
            alg->bandwidth_list[i] = alg->bandwidth_list[i + 1];
        }
        alg->bandwidth_list[4] = alg->bandwidth;
    }
    for (int i = 0; i < alg->bw_cnt - 1; ++i) {
        alg->bandwidth += alg->bandwidth_list[i];
    }
    alg->bandwidth /= alg->bw_cnt;

    sheduler_ctx *this_s_ctx = client->get_s_ctx(client);
    this_s_ctx->bandwidth = alg->bandwidth;
    // printf("\033[34m[DEBUG]\033[0m %ld %ld %ld %ld %p\n", this_s_ctx->base_loc, this_s_ctx->downloaded_size, 
    //     full_size, this_s_ctx->will_be_downloaded_size, file_buf);
    memcpy(file_buf + this_s_ctx->base_loc + this_s_ctx->downloaded_size, (*client->buf)->bytes,
        MIN((*client->buf)->size, this_s_ctx->will_be_downloaded_size));
    this_s_ctx->downloaded_size += (*client->buf)->size;
    this_s_ctx->will_be_downloaded_size -= MIN((*client->buf)->size, this_s_ctx->will_be_downloaded_size);
    this_s_ctx->time_left = this_s_ctx->will_be_downloaded_size * 1.0 / (alg->bandwidth * 1024 * 1.024);
    // fwrite((*client->buf)->bytes, 1, (*client->buf)->size, save_file);
    h2o_buffer_consume(&(*client->buf), (*client->buf)->size);
    // printf("\033[35m[DEBUG]\033[0m %ld\n", this_s_ctx->will_be_downloaded_size);

    int this_idx = -1;
    for (int i = 0; i < 3; ++i) {
        if (&s_ctx[i] == this_s_ctx) this_idx = i;
    }
    if (this_idx == -1) {
        printf("\033[31m[ERROR]\033[0m\n");
        int __e;
        scanf("%d", &__e);
    }
    char text[2048] = {0};
    double time_elapsed = (now.tv_sec - file_start.tv_sec) * 1000
                    + (now.tv_usec - file_start.tv_usec) * 1.0 / 1000;
    sprintf(text, "%lf %lf\n", time_elapsed, 
        (this_s_ctx->base_loc + this_s_ctx->downloaded_size)*1.0 / 1024);
    fwrite(text, 1, strlen(text), dat_file[this_idx]);


    if (this_s_ctx->time_left <= alg->rtt && this_s_ctx->num_streams < 2) {
        this_s_ctx->will_done = 1;
        int max_idx = 0;
        double max_val = 0;
        for (int i = 0; i < 3; ++i) {
            if (i == this_idx) continue;
            if (s_ctx[i].time_left > MIN(delta, alg->rtt)) {
                this_s_ctx->will_done = 0;
            }
            if (s_ctx[i].time_left > max_val) max_idx = i, max_val = s_ctx[i].time_left;
        }
        if (!this_s_ctx->will_done) {
            if (s_ctx[max_idx].will_be_downloaded_size <= alg->rtt * s_ctx->bandwidth) {
                printf("\033[31m[qwq]\033[0m\n");
                return 0;
            }
            this_s_ctx->num_streams += 1;
            size_t tmp = s_ctx[max_idx].will_be_downloaded_size - alg->rtt * s_ctx->bandwidth;
            size_t continue_size = tmp * s_ctx[max_idx].alg->bandwidth / 
                (s_ctx[max_idx].alg->bandwidth + alg->bandwidth);
            size_t new_stream_size = tmp - continue_size;
            printf("\033[31m[split]\033[0m continue: %ld new: %ld this_idx %d max-idx %d\n", 
                continue_size, new_stream_size, this_idx, max_idx);
            s_ctx[max_idx].will_be_downloaded_size -= new_stream_size;
            s_ctx[max_idx].time_left -= new_stream_size * 1.0 / (s_ctx[max_idx].alg->bandwidth * 1024 * 1.024);
            this_s_ctx->range[0] = s_ctx[max_idx].base_loc + s_ctx[max_idx].downloaded_size + 
                s_ctx[max_idx].will_be_downloaded_size;
            this_s_ctx->range[1] = this_s_ctx->range[0] + new_stream_size;
            start_request(&ctx[this_idx], ctx[this_idx].url);
        }
    }

    if (this_s_ctx->will_be_downloaded_size == 0) {
        this_s_ctx->num_streams -= 1;
        is_all_done += 1;
        printf("\033[32m[is_all_done: %d this_idx: %d all_done_flag: %d]\033[0m\n", 
            is_all_done, this_idx, all_done_flag);
        return -1;
    }
    return 0;
}

static void print_status_line(int version, int status, h2o_iovec_t msg)
{
    printf("HTTP/%d", (version >> 8));
    if ((version & 0xff) != 0) {
        printf(".%d", version & 0xff);
    }
    printf(" %d", status);
    if (msg.len != 0) {
        printf(" %.*s\n", (int)msg.len, msg.base);
    } else {
        printf("\n");
    }
}

h2o_httpclient_body_cb on_head(h2o_httpclient_t *client, const char *errstr, int version, int status, h2o_iovec_t msg,
                               h2o_header_t *headers, size_t num_headers, int header_requires_dup)
{
    size_t i;
    if (errstr != NULL && errstr != h2o_httpclient_error_is_eos) {
        on_error(client->ctx, errstr);
        return NULL;
    }

    print_status_line(version, status, msg);

    for (i = 0; i != num_headers; ++i) {
        const char *name = headers[i].orig_name;
        if (name == NULL)
            name = headers[i].name->base;
        if (is_first_stream && strcmp(name, "content-length") == 0) {
            full_size = atol(headers[i].value.base);
            size_t chunk = full_size / 3;
            client->ctx->s_ctx->will_be_downloaded_size = chunk;
            // printf("\033[33m[DEBUG]\033[0m get length: %ld\n", full_size);
            if (is_first_stream) {
                // printf("\033[34m[DEBUG]\033[0m first get head\n");
                file_buf = malloc(full_size);
                memset(file_buf, 0, full_size);
                is_first_stream = 0;
            }
            s_ctx[0].base_loc = 0;
            s_ctx[0].downloaded_size = 0;
            s_ctx[0].will_be_downloaded_size = chunk;
            s_ctx[0].is_range = 1;

            s_ctx[1].base_loc = chunk;
            s_ctx[1].downloaded_size = 0;
            s_ctx[1].will_be_downloaded_size = chunk;
            s_ctx[1].is_range = 1;
            s_ctx[1].range[0] = chunk;
            s_ctx[1].range[1] = 2 * chunk;

            s_ctx[2].base_loc = chunk * 2;
            s_ctx[2].downloaded_size = 0;
            s_ctx[2].will_be_downloaded_size = full_size - chunk * 2;
            s_ctx[2].is_range = 1;
            s_ctx[2].range[0] = 2 * chunk;
            s_ctx[2].range[1] = full_size;

            start_request(&ctx[1], url2);
            start_request(&ctx[2], url3);
        }
        printf("%.*s: %.*s\n", (int)headers[i].name->len, name, (int)headers[i].value.len, headers[i].value.base);
    }
    printf("\n");

    if (errstr == h2o_httpclient_error_is_eos) {
        on_error(client->ctx, "no body");
        return NULL;
    }

    return on_body;
}

int fill_body(h2o_iovec_t *reqbuf)
{
    if (cur_body_size > 0) {
        memcpy(reqbuf, &iov_filler, sizeof(*reqbuf));
        reqbuf->len = MIN(iov_filler.len, cur_body_size);
        cur_body_size -= reqbuf->len;
        return 0;
    } else {
        *reqbuf = h2o_iovec_init(NULL, 0);
        return 1;
    }
}

struct st_timeout_ctx {
    h2o_httpclient_t *client;
    h2o_timer_t _timeout;
};
static void timeout_cb(h2o_timer_t *entry)
{
    static h2o_iovec_t reqbuf;
    struct st_timeout_ctx *tctx = H2O_STRUCT_FROM_MEMBER(struct st_timeout_ctx, _timeout, entry);

    fill_body(&reqbuf);
    h2o_timer_unlink(&tctx->_timeout);
    tctx->client->write_req(tctx->client, reqbuf, cur_body_size <= 0);
    free(tctx);

    return;
}

static void proceed_request(h2o_httpclient_t *client, size_t written, int is_end_stream)
{
    if (cur_body_size > 0) {
        struct st_timeout_ctx *tctx;
        tctx = h2o_mem_alloc(sizeof(*tctx));
        memset(tctx, 0, sizeof(*tctx));
        tctx->client = client;
        tctx->_timeout.cb = timeout_cb;
        h2o_timer_link(client->ctx->loop, delay_interval_ms, &tctx->_timeout);
    }
}

h2o_httpclient_head_cb on_connect(h2o_httpclient_t *client, const char *errstr, h2o_iovec_t *_method, h2o_url_t *url,
                                  const h2o_header_t **headers, size_t *num_headers, h2o_iovec_t *body,
                                  h2o_httpclient_proceed_req_cb *proceed_req_cb, h2o_httpclient_properties_t *props,
                                  h2o_url_t *origin)
{
    if (errstr != NULL) {
        on_error(client->ctx, errstr);
        return NULL;
    }
    gettimeofday(&((client->get_alg(client))->all_start_time), NULL);
    *_method = h2o_iovec_init(method, strlen(method));
    *url = *((h2o_url_t *)client->data);
    *headers = NULL;
    *num_headers = 0;
    *body = h2o_iovec_init(NULL, 0);
    *proceed_req_cb = NULL;
    h2o_headers_t headers_vec = (h2o_headers_t){NULL};
    if (client->ctx->s_ctx->is_range) {
        sheduler_ctx *tmp_s_ctx = client->ctx->s_ctx;
        char msg_header[2048] = {0};
        size_t msg_len = sprintf(msg_header, "bytes=%ld-%ld", tmp_s_ctx->range[0], tmp_s_ctx->range[1] - 1);
        printf("\033[31m[msg]\033[0m %s\n",msg_header);
        h2o_add_header(&pool, &headers_vec, H2O_TOKEN_RANGE, NULL, msg_header, msg_len);
        *headers = headers_vec.entries;
        *num_headers += 1;
    }
    if (cur_body_size > 0) {
        char *clbuf = h2o_mem_alloc_pool(&pool, char, sizeof(H2O_UINT32_LONGEST_STR) - 1);
        size_t clbuf_len = sprintf(clbuf, "%d", cur_body_size);
        // printf("\033[33m[DEBUG]\033[0m %s %ld\n", clbuf, clbuf_len);
        h2o_add_header(&pool, &headers_vec, H2O_TOKEN_CONTENT_LENGTH, NULL, clbuf, clbuf_len);
        *headers = headers_vec.entries;
        *num_headers += 1;

        *proceed_req_cb = proceed_request;

        struct st_timeout_ctx *tctx;
        tctx = h2o_mem_alloc(sizeof(*tctx));
        memset(tctx, 0, sizeof(*tctx));
        tctx->client = client;
        tctx->_timeout.cb = timeout_cb;
        h2o_timer_link(client->ctx->loop, delay_interval_ms, &tctx->_timeout);
    }

    return on_head;
}

static void usage(const char *progname)
{
    fprintf(stderr,
            "Usage: [-t <server file path> default: /index.html] [-m <method>] [-b <body size>] [-c <chunk size>] [-i <interval between chunks>] [-o <saved file path>]%s <url1> <url2> <url3>\n",
            progname);
}
static void process_url(char * url_, char * u) {
    char * url__ = url_;
    if (!(u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p' && u[4] == 's')) {
        sprintf(url__, "https://");
        url__ += 8;
    }
    if (server_file_path != NULL) {
        sprintf(url__, "%s/%s", u, server_file_path);
    }
    else {
        sprintf(url__, "%s", u);
    }
    printf("%s\n", url_);
} 

/*Begin of my functions*/


/*End of my functions*/


int main(int argc, char **argv)
{
    gettimeofday(&file_start, NULL);
    h2o_multithread_queue_t *queue;
    h2o_multithread_receiver_t getaddr_receiver;

    const uint64_t timeout = 5000; /* 5 seconds */
    h2o_httpclient_ctx_t _ctx[3] = {{
        NULL, /* loop */
        &getaddr_receiver,
        timeout,                                 /* io_timeout */
        timeout,                                 /* connect_timeout */
        timeout,                                 /* first_byte_timeout */
        NULL,                                    /* websocket_timeout */
        0,                                       /* keepalive_timeout */
        H2O_SOCKET_INITIAL_INPUT_BUFFER_SIZE * 2, /* max_buffer_size */
        &s_ctx[0],
        url1
    }, {
        NULL, /* loop */
        &getaddr_receiver,
        timeout,                                 /* io_timeout */
        timeout,                                 /* connect_timeout */
        timeout,                                 /* first_byte_timeout */
        NULL,                                    /* websocket_timeout */
        0,                                       /* keepalive_timeout */
        H2O_SOCKET_INITIAL_INPUT_BUFFER_SIZE * 2, /* max_buffer_size */
        &s_ctx[1],
        url2
    }, {
        NULL, /* loop */
        &getaddr_receiver,
        timeout,                                 /* io_timeout */
        timeout,                                 /* connect_timeout */
        timeout,                                 /* first_byte_timeout */
        NULL,                                    /* websocket_timeout */
        0,                                       /* keepalive_timeout */
        H2O_SOCKET_INITIAL_INPUT_BUFFER_SIZE * 2, /* max_buffer_size */
        &s_ctx[2],
        url3
    }};
    ctx = _ctx;
    int opt;

    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    while ((opt = getopt(argc, argv, "o:t:m:b:c:i:r:k")) != -1) {
        switch (opt) {
        case 't':
            server_file_path = malloc(strlen(optarg) + 1);
            strcpy(server_file_path, optarg);
            break;
        case 'o':
            save_path = malloc(strlen(optarg) + 1);
            strcpy(save_path, optarg);
            break;
        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }
    if (argc - optind != 3 || strlen(argv[optind]) < 7 || 
        strlen(argv[optind + 1]) < 7 || strlen(argv[optind + 2]) < 7) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    process_url(url1, argv[optind]);
    process_url(url2, argv[optind + 1]);
    process_url(url3, argv[optind + 2]);

    if (body_size != 0) {
        iov_filler.base = h2o_mem_alloc(chunk_size);
        memset(iov_filler.base, 'a', chunk_size);
        iov_filler.len = chunk_size;
    }
    h2o_mem_init_pool(&pool);

    if (save_path != NULL) {
        save_file = fopen(save_path, "wb");
        if (save_file == NULL) {
            printf("Some error occured when opening this file!\n");
            if (errno == ENOENT) {
                printf("No such file or dictionary!\n");
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        dat_file[i] = fopen(dat_file_name[i], "w");
    }
    

    ctx[2].http2.ratio = ctx[1].http2.ratio = ctx[0].http2.ratio = http2_ratio;

/* setup context */
#if H2O_USE_LIBUV
    printf("\033[32m[INFO]\033[0m You are using libuv\n");
    ctx[0].loop = uv_loop_new();
#else
    printf("\033[32m[INFO]\033[0m You are \033[31mnot\033[0m Using libuv\n");
    ctx[0].loop = h2o_evloop_create();
#endif

    ctx[1].loop = ctx[2].loop = ctx[0].loop;

    queue = h2o_multithread_create_queue(ctx[0].loop);
    h2o_multithread_register_receiver(queue, ctx[0].getaddr_receiver, h2o_hostinfo_getaddr_receiver);

    /* setup the first request */
    start_request(&ctx[0], url1);
    while (is_all_done != all_done_flag) {
#if H2O_USE_LIBUV
        uv_run(ctx[0].loop, UV_RUN_ONCE);
#else
        h2o_evloop_run(ctx[0].loop, INT32_MAX);
#endif
    }

    if (save_path != NULL && save_file != NULL) {
        fwrite(file_buf, 1, full_size, save_file);
        fclose(save_file);
    }
    gettimeofday(&file_end, NULL);
    printf("\033[32m[INFO]\033[0m [Full time: %lf ms]\n", (file_end.tv_sec - file_start.tv_sec) * 1000.0 + 
        (file_end.tv_usec - file_start.tv_usec) * 1.0 / 1000);
    return 0;
}
