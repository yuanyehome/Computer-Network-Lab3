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
#ifndef h2o__http2_h
#define h2o__http2_h

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include "khash.h"
#include "./http2/scheduler.h"

typedef struct st_h2o_http2_conn_t h2o_http2_conn_t;
typedef struct st_h2o_http2_stream_t h2o_http2_stream_t;

extern const char *h2o_http2_npn_protocols;
extern const h2o_iovec_t *h2o_http2_alpn_protocols;

/* connection flow control window + alpha */
#define H2O_HTTP2_DEFAULT_OUTBUF_SIZE 81920

/* defined as negated form of the error codes defined in HTTP2-spec section 7 */
#define H2O_HTTP2_ERROR_NONE 0
#define H2O_HTTP2_ERROR_PROTOCOL -1
#define H2O_HTTP2_ERROR_INTERNAL -2
#define H2O_HTTP2_ERROR_FLOW_CONTROL -3
#define H2O_HTTP2_ERROR_SETTINGS_TIMEOUT -4
#define H2O_HTTP2_ERROR_STREAM_CLOSED -5
#define H2O_HTTP2_ERROR_FRAME_SIZE -6
#define H2O_HTTP2_ERROR_REFUSED_STREAM -7
#define H2O_HTTP2_ERROR_CANCEL -8
#define H2O_HTTP2_ERROR_COMPRESSION -9
#define H2O_HTTP2_ERROR_CONNECT -10
#define H2O_HTTP2_ERROR_ENHANCE_YOUR_CALM -11
#define H2O_HTTP2_ERROR_INADEUATE_SECURITY -12
#define H2O_HTTP2_ERROR_INCOMPLETE -255 /* an internal value indicating that all data is not ready */
#define H2O_HTTP2_ERROR_PROTOCOL_CLOSE_IMMEDIATELY -256

/* hpack */

#define H2O_HTTP2_ENCODE_INT_MAX_LENGTH 5

typedef struct st_h2o_hpack_header_table_t {
    /* ring buffer */
    struct st_h2o_hpack_header_table_entry_t *entries;
    size_t num_entries, entry_capacity, entry_start_index;
    /* size and capacities are 32+name_len+value_len (as defined by hpack spec.) */
    size_t hpack_size;
    size_t hpack_capacity;     /* the value set by SETTINGS_HEADER_TABLE_SIZE _and_ dynamic table size update */
    size_t hpack_max_capacity; /* the value set by SETTINGS_HEADER_TABLE_SIZE */
} h2o_hpack_header_table_t;

void h2o_hpack_dispose_header_table(h2o_hpack_header_table_t *header_table);
int h2o_hpack_parse_headers(h2o_req_t *req, h2o_hpack_header_table_t *header_table, int *allow_psuedo, const uint8_t *src,
                            size_t len);
size_t h2o_hpack_encode_string(uint8_t *dst, const char *s, size_t len);
int h2o_hpack_flatten_headers(h2o_buffer_t **buf, h2o_hpack_header_table_t *header_table, uint32_t stream_id, size_t max_frame_size,
                              h2o_res_t *res, h2o_timestamp_t *ts, const h2o_iovec_t *server_name);

/* settings */

#define H2O_HTTP2_SETTINGS_HEADER_TABLE_SIZE 1
#define H2O_HTTP2_SETTINGS_ENABLE_PUSH 2
#define H2O_HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 3
#define H2O_HTTP2_SETTINGS_INITIAL_WINDOW_SIZE 4
#define H2O_HTTP2_SETTINGS_MAX_FRAME_SIZE 5
#define H2O_HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE 6

typedef struct st_h2o_http2_settings_t {
    uint32_t header_table_size;
    uint32_t enable_push;
    uint32_t max_concurrent_streams;
    uint32_t initial_window_size;
    uint32_t max_frame_size;
} h2o_http2_settings_t;

const h2o_http2_settings_t H2O_HTTP2_SETTINGS_DEFAULT;
const h2o_http2_settings_t H2O_HTTP2_SETTINGS_HOST;

typedef struct st_h2o_http2_priority_t {
    int exclusive;
    uint32_t dependency; /* 0 if not set */
    uint16_t weight;     /* 0 if not set */
} h2o_http2_priority_t;

/* frames */

#define H2O_HTTP2_FRAME_HEADER_SIZE 9

#define H2O_HTTP2_FRAME_TYPE_DATA 0
#define H2O_HTTP2_FRAME_TYPE_HEADERS 1
#define H2O_HTTP2_FRAME_TYPE_PRIORITY 2
#define H2O_HTTP2_FRAME_TYPE_RST_STREAM 3
#define H2O_HTTP2_FRAME_TYPE_SETTINGS 4
#define H2O_HTTP2_FRAME_TYPE_PUSH_PROMISE 5
#define H2O_HTTP2_FRAME_TYPE_PING 6
#define H2O_HTTP2_FRAME_TYPE_GOAWAY 7
#define H2O_HTTP2_FRAME_TYPE_WINDOW_UPDATE 8
#define H2O_HTTP2_FRAME_TYPE_CONTINUATION 9

#define H2O_HTTP2_FRAME_FLAG_END_STREAM 0x1
#define H2O_HTTP2_FRAME_FLAG_ACK 0x1
#define H2O_HTTP2_FRAME_FLAG_END_HEADERS 0x4
#define H2O_HTTP2_FRAME_FLAG_PADDED 0x8
#define H2O_HTTP2_FRAME_FLAG_PRIORITY 0x20

typedef struct st_h2o_http2_frame_t {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
    const uint8_t *payload;
} h2o_http2_frame_t;

typedef struct st_h2o_http2_data_payload_t {
    const uint8_t *data;
    size_t length;
} h2o_http2_data_payload_t;

typedef struct st_h2o_http2_headers_payload_t {
    h2o_http2_priority_t priority;
    const uint8_t *headers;
    size_t headers_len;
} h2o_http2_headers_payload_t;

typedef struct st_h2o_http2_rst_stream_payload_t {
    uint32_t error_code;
} h2o_http2_rst_stream_payload_t;

typedef struct st_h2o_http2_ping_payload_t {
    uint8_t data[8];
} h2o_http2_ping_payload_t;

typedef struct st_h2o_http2_goaway_payload_t {
    uint32_t last_stream_id;
    uint32_t error_code;
    h2o_iovec_t debug_data;
} h2o_http2_goaway_payload_t;

typedef struct st_h2o_http2_window_update_payload_t {
    uint32_t window_size_increment;
} h2o_http2_window_update_payload_t;

typedef struct st_h2o_http2_window_t {
    ssize_t _avail;
} h2o_http2_window_t;

typedef enum enum_h2o_http2_stream_state_t {
    H2O_HTTP2_STREAM_STATE_RECV_PSUEDO_HEADERS,
    H2O_HTTP2_STREAM_STATE_RECV_HEADERS,
    H2O_HTTP2_STREAM_STATE_RECV_BODY,
    H2O_HTTP2_STREAM_STATE_REQ_PENDING,
    H2O_HTTP2_STREAM_STATE_SEND_HEADERS,
    H2O_HTTP2_STREAM_STATE_SEND_BODY,
    H2O_HTTP2_STREAM_STATE_END_STREAM
} h2o_http2_stream_state_t;

struct st_h2o_http2_stream_t {
    uint32_t stream_id;
    int is_half_closed;
    h2o_ostream_t _ostr_final;
    h2o_http2_stream_state_t state;
    h2o_http2_window_t output_window;
    h2o_http2_window_t input_window;
    h2o_http2_priority_t priority;
    h2o_buffer_t *_req_body;
    H2O_VECTOR(h2o_iovec_t) _data;
    h2o_ostream_pull_cb _pull_cb;
    /* link list governed by connection.c for handling various things */
    struct {
        h2o_linklist_t link;
        h2o_http2_scheduler_slot_t *slot;
    } _link;
    /* placed at last since it is large and has it's own ctor */
    h2o_req_t req;
};

KHASH_MAP_INIT_INT64(h2o_http2_stream_t, h2o_http2_stream_t *)

typedef enum enum_h2o_http2_conn_state_t { H2O_HTTP2_CONN_STATE_OPEN, H2O_HTTP2_CONN_STATE_IS_CLOSING } h2o_http2_conn_state_t;

struct st_h2o_http2_conn_t {
    h2o_conn_t super;
    h2o_socket_t *sock;
    /* settings */
    h2o_http2_settings_t peer_settings;
    /* streams */
    khash_t(h2o_http2_stream_t) * open_streams;
    uint32_t max_open_stream_id;
    uint32_t max_processed_stream_id;
    uint32_t num_responding_streams;
    /* internal */
    h2o_http2_conn_state_t state;
    ssize_t (*_read_expect)(h2o_http2_conn_t *conn, const uint8_t *src, size_t len);
    h2o_buffer_t *_http1_req_input; /* contains data referred to by original request via HTTP/1.1 */
    h2o_hpack_header_table_t _input_header_table;
    h2o_http2_window_t _input_window;
    h2o_hpack_header_table_t _output_header_table;
    h2o_linklist_t _pending_reqs; /* list of h2o_http2_stream_t that contain pending requests */
    h2o_timeout_entry_t _timeout_entry;
    struct {
        h2o_buffer_t *buf;
        h2o_buffer_t *buf_in_flight;
        h2o_http2_scheduler_t scheduler;
        h2o_linklist_t streams_to_proceed;
        h2o_timeout_entry_t timeout_entry;
        h2o_http2_window_t window;
    } _write;
};

int h2o_http2_update_peer_settings(h2o_http2_settings_t *settings, const uint8_t *src, size_t len);

/* frames */
uint8_t *h2o_http2_encode_frame_header(uint8_t *dst, size_t length, uint8_t type, uint8_t flags, int32_t stream_id);
void h2o_http2_encode_rst_stream_frame(h2o_buffer_t **buf, uint32_t stream_id, int errnum);
void h2o_http2_encode_ping_frame(h2o_buffer_t **buf, int is_ack, const uint8_t *data);
void h2o_http2_encode_goaway_frame(h2o_buffer_t **buf, uint32_t last_stream_id, int errnum, h2o_iovec_t additional_data);
void h2o_http2_encode_window_update_frame(h2o_buffer_t **buf, uint32_t stream_id, int32_t window_size_increment);
ssize_t h2o_http2_decode_frame(h2o_http2_frame_t *frame, const uint8_t *src, size_t len, const h2o_http2_settings_t *host_settings);
int h2o_http2_decode_data_payload(h2o_http2_data_payload_t *payload, const h2o_http2_frame_t *frame);
int h2o_http2_decode_headers_payload(h2o_http2_headers_payload_t *payload, const h2o_http2_frame_t *frame);
int h2o_http2_decode_rst_stream_payload(h2o_http2_rst_stream_payload_t *payload, const h2o_http2_frame_t *frame);
int h2o_http2_decode_ping_payload(h2o_http2_ping_payload_t *payload, const h2o_http2_frame_t *frame);
int h2o_http2_decode_goaway_payload(h2o_http2_goaway_payload_t *payload, const h2o_http2_frame_t *frame);
int h2o_http2_decode_window_update_payload(h2o_http2_window_update_payload_t *paylaod, const h2o_http2_frame_t *frame);

/* connection */
void h2o_http2_conn_register_stream(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream);
void h2o_http2_conn_unregister_stream(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream);
static h2o_http2_stream_t *h2o_http2_conn_get_stream(h2o_http2_conn_t *conn, uint32_t stream_id);
void h2o_http2_accept(h2o_context_t *ctx, h2o_socket_t *sock);
int h2o_http2_handle_upgrade(h2o_req_t *req);
void h2o_http2_conn_request_write(h2o_http2_conn_t *conn);
void h2o_http2_conn_register_for_proceed_callback(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream);
static ssize_t h2o_http2_conn_get_buffer_window(h2o_http2_conn_t *conn);

/* stream */
h2o_http2_stream_t *h2o_http2_stream_open(h2o_http2_conn_t *conn, uint32_t stream_id, const h2o_http2_priority_t *priority,
                                          h2o_req_t *src_req);
void h2o_http2_stream_close(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream);
void h2o_http2_stream_reset(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream, int errnum);
void h2o_http2_stream_send_pending_data(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream);
static int h2o_http2_stream_has_pending_data(h2o_http2_stream_t *stream);
void h2o_http2_stream_proceed(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream);

/* misc */
static void h2o_http2_window_init(h2o_http2_window_t *window, const h2o_http2_settings_t *peer_settings);
static void h2o_http2_window_update(h2o_http2_window_t *window, ssize_t delta);
static ssize_t h2o_http2_window_get_window(h2o_http2_window_t *window);
static void h2o_http2_window_consume_window(h2o_http2_window_t *window, size_t bytes);

/* inline definitions */

inline h2o_http2_stream_t *h2o_http2_conn_get_stream(h2o_http2_conn_t *conn, uint32_t stream_id)
{
    khiter_t iter = kh_get(h2o_http2_stream_t, conn->open_streams, stream_id);
    if (iter != kh_end(conn->open_streams))
        return kh_val(conn->open_streams, iter);
    return NULL;
}

inline ssize_t h2o_http2_conn_get_buffer_window(h2o_http2_conn_t *conn)
{
    ssize_t ret, winsz;

    ret = conn->_write.buf->capacity - conn->_write.buf->size;
    if (ret < H2O_HTTP2_FRAME_HEADER_SIZE)
        return 0;
    ret -= H2O_HTTP2_FRAME_HEADER_SIZE;
    winsz = h2o_http2_window_get_window(&conn->_write.window);
    if (winsz < ret)
        ret = winsz;
    return ret;
}

inline int h2o_http2_stream_has_pending_data(h2o_http2_stream_t *stream)
{
    return stream->_data.size != 0;
}

inline void h2o_http2_window_init(h2o_http2_window_t *window, const h2o_http2_settings_t *peer_settings)
{
    window->_avail = peer_settings->initial_window_size;
}

inline void h2o_http2_window_update(h2o_http2_window_t *window, ssize_t delta)
{
    window->_avail += delta;
}

inline ssize_t h2o_http2_window_get_window(h2o_http2_window_t *window)
{
    return window->_avail;
}

inline void h2o_http2_window_consume_window(h2o_http2_window_t *window, size_t bytes)
{
    window->_avail -= bytes;
}

#ifdef __cplusplus
}
#endif

#endif
