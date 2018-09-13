#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <time.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#define INTERVAL 150
#define REQUEST_COUNT 1000

#define SERVER_PORT 7000
#define MESSAGE_SIZE 64
#define BUFFER_SIZE 65536

char *server, global_buf[BUFFER_SIZE];
uv_loop_t *loop;
uv_tcp_t* tcp;
uv_stream_t* on_going_stream;
int request_count, local_port, global_buf_offset;

void current_time(struct timespec *tp) {
#ifdef __MACH__
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    tp->tv_sec = mts.tv_sec;
    tp->tv_nsec = mts.tv_nsec;
#else
    clock_gettime(CLOCK_MONOTONIC, tp);
#endif
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = global_buf + global_buf_offset;
    buf->len = BUFFER_SIZE - global_buf_offset;
}

void on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    if (nread < 0) {
        if(nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_strerror(nread));
        uv_close((uv_handle_t*) client, NULL);
        return;
    }
    int offset = 0, i;
    struct timespec start, end;
    double elasped_nsec;
    current_time(&end);
    while (offset < global_buf_offset + nread && global_buf[offset] == 'S') {
        if (offset + MESSAGE_SIZE > global_buf_offset + nread) {
            global_buf_offset += nread - offset;
            for (i=0;i<global_buf_offset;i++) {
                global_buf[i] = global_buf[offset+i];
            }
            break;
        }
        start.tv_sec = 0;
        for (i=offset+1;i<=offset+16;i++) {
            start.tv_sec += (buf->base[i]&0xff)<<(8*(i-offset-1));
        }
        start.tv_nsec = 0;
        for (i=offset+17;i<=offset+32;i++) {
            start.tv_nsec += (buf->base[i]&0xff)<<(8*(i-offset-17));
        }
        elasped_nsec = (end.tv_sec-start.tv_sec)*1e9
            + end.tv_nsec-start.tv_nsec;
        fprintf(stdout, "%.4f\n", elasped_nsec / 1e6);
        if (elasped_nsec > 500 * 1e6) {
            fprintf(stderr, "%d %zu %zu %.4f\n",
                    local_port, start.tv_sec, start.tv_nsec,
                    elasped_nsec / 1e6);
        }
        offset += MESSAGE_SIZE;
    }
}

void on_write(uv_write_t *req, int status) {
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
        return;
    }
}

void on_connect(uv_connect_t* req, int status) {
    if (status) {
        fprintf(stderr, "Connect error %s\n", uv_strerror(status));
        return;
    }

    on_going_stream = req->handle;
    free(req);
    uv_read_start(on_going_stream, alloc_buffer, on_read);
}

void send_request(uv_timer_t *handle) {
    request_count++;
    if (request_count > REQUEST_COUNT) {
        if (tcp != NULL && !uv_is_closing((uv_handle_t *) tcp))
            uv_close((uv_handle_t *) tcp, NULL);
        uv_timer_stop(handle);
        return;
    }
    if (on_going_stream != NULL) {
        char message[MESSAGE_SIZE];
        struct timespec tp;
        int i;
        message[0] = 'S';
        current_time(&tp);
        for (i=1;i<=16;i++) {
            message[i] = tp.tv_sec & 0xff;
            tp.tv_sec >>= 8;
        }
        for (i=17;i<=32;i++) {
            message[i] = tp.tv_nsec & 0xff;
            tp.tv_nsec >>= 8;
        }
        uv_buf_t bufs[] = {
            { .len  = MESSAGE_SIZE, .base = message }
        };

        uv_write_t write_req;
        uv_write(&write_req, on_going_stream, bufs, 1, on_write);
    }
}

void new_client() {
    tcp = malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, tcp);
    uv_tcp_nodelay(tcp, 1);
    // uv_tcp_keepalive(tcp, 1, 60);

    struct sockaddr_in addr;
    uv_ip4_addr(server, SERVER_PORT, &addr);
    uv_connect_t *conn = malloc(sizeof(uv_connect_t));
    uv_tcp_connect(conn, tcp, (const struct sockaddr*)&addr, on_connect);

    struct sockaddr_in sin;
    int sinlen = sizeof(sin);
    uv_tcp_getsockname(tcp, (struct sockaddr *) &sin, &sinlen);
    local_port = ntohs(sin.sin_port);
}

int main(int argc, char **argv) {
    srand(time(NULL));
    tcp = NULL;
    on_going_stream = NULL;
    request_count = 0;
    server = argv[1];

    loop = uv_default_loop();

    uv_timer_t timer_req;
    uv_timer_init(loop, &timer_req);
    uv_timer_start(&timer_req, send_request, 5000 + rand()%3000, INTERVAL);

    new_client();
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return 0;
}
