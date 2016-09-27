#include "socket.c"
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <rpc/types.h>
#include <sys/param.h>
#include <assert.h>

#include <ev.h>

#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define METHOD_POST 4
#define PROGRAM_VERSION "0.0.3"

ev_io ev_request_watcher;
ev_timer ev_timeout_watcher;

extern int errno;


struct global_bench_config {
    int request_method;
    int request_count;
    int bench_time;
    int keepalive;
    int force;
    int client_count;
    int pipe[2];
    char *url;
    char *user_agent;
} global_bench_config;

int is_time_expired = 0;

#define REQUEST_SIZE 2048
struct bench_request {
    int port;
    int header_len;
    char host[MAXHOSTNAMELEN];
    char header[REQUEST_SIZE];
} request;

struct bench_result {
    int speed;
    int failed;
    int bytes;
} bench_result;

static void start_bench();
static void bench_core(int event_count);
static void build_request();
static void init_config(int, char **);
static void usage();

char tmp_read_buf[1500];

/* #define DUMP_STACK_DEPTH_MAX 16 */
/* void dump_trace() { */
/*     void *stack_trace[DUMP_STACK_DEPTH_MAX] = {0}; */
/*     char **stack_strings = NULL; */
/*     int stack_depth = 0; */
/*     int i = 0; */

/*     /1* 获取栈中各层调用函数地址 *1/ */
/*     stack_depth = backtrace(stack_trace, DUMP_STACK_DEPTH_MAX); */

/*     /1* 查找符号表将函数调用地址转换为函数名称 *1/ */
/*     stack_strings = (char **)backtrace_symbols(stack_trace, stack_depth); */
/*     if (NULL == stack_strings) { */
/*         printf(" Memory is not enough while dump Stack Trace! \r\n"); */
/*         return; */
/*     } */

/*     /1* 打印调用栈 *1/ */
/*     printf(" Stack Trace: \r\n"); */
/*     for (i = 0; i < stack_depth; ++i) { */
/*         printf(" [%d] %s \r\n", i, stack_strings[i]); */
/*     } */

/*     /1* 获取函数名称时申请的内存需要自行释放 *1/ */
/*     free(stack_strings); */
/*     stack_strings = NULL; */

/*     return; */
/* } */

int main(int argc, char **argv)
{
    if (argc==1) {
        usage();
        return 2;
    }
    init_config(argc, argv);

    build_request();

    start_bench();

    return 0;
}

static void init_config(int argc, char **argv)
{
    int opt=0;
    int options_index=0;

    global_bench_config.request_method = METHOD_GET;
    global_bench_config.request_count = 1;
    global_bench_config.bench_time = 30;
    global_bench_config.keepalive = 0;
    global_bench_config.force = 0;

    const struct option long_options[]=
    {
        {"force",no_argument,&global_bench_config.force,1},
        {"time",required_argument,NULL,'t'},
        {"user-agent",required_argument,NULL,'u'},
        {"help",no_argument,NULL,'?'},
        {"get",no_argument,&global_bench_config.request_method,METHOD_GET},
        {"head",no_argument,&global_bench_config.request_method,METHOD_HEAD},
        {"options",no_argument,&global_bench_config.request_method,METHOD_OPTIONS},
        {"trace",no_argument,&global_bench_config.request_method,METHOD_TRACE},
        {"clients",required_argument,NULL,'c'},
        {"keepalive",no_argument,&global_bench_config.keepalive,1},
        {"version",no_argument,NULL,'V'},
        {NULL,0,NULL,0}
    };

    while((opt=getopt_long(argc,argv,"Vfku:t:c:?h",long_options,&options_index))!=EOF )
    {
        switch(opt)
        {
            case  0 : break;
            case 'V': printf(PROGRAM_VERSION"\n");exit(0);
            case 'f': global_bench_config.force = 1;break;
            case 'k': global_bench_config.keepalive = 1;break;
            case 't': global_bench_config.bench_time = atoi(optarg);break;
            case 'u': global_bench_config.user_agent = strdup(optarg);break;
            case 'c': global_bench_config.client_count = atoi(optarg);break;
            case ':':
            case 'h':
            case '?': usage();exit(0);
        }
    }
    global_bench_config.url = strdup(argv[optind]);
}

static void usage(void)
{
    fprintf(stderr,
            "webbench [option]... URL\n"
            "  -f|--force               Don't wait for reply from server.\n"
            "  -r|--reload              Send reload request - Pragma: no-cache.\n"
            "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
            "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
            "  --get                    Use GET request method.\n"
            "  --head                   Use HEAD request method.\n"
            "  --options                Use OPTIONS request method.\n"
            "  --trace                  Use TRACE request method.\n"
            "  -?|-h|--help             This information.\n"
            "  -V|--version             Display program version.\n"
           );
};

static void build_request()
{

    const char *url = global_bench_config.url;
    memset(request.host, 0, MAXHOSTNAMELEN);
    memset(request.header, 0, REQUEST_SIZE);

    switch (global_bench_config.request_method) {
        case METHOD_GET:
            strcpy(request.header, "GET");
            break;
        case METHOD_HEAD:
            strcpy(request.header, "HEAD");
            break;
        case METHOD_OPTIONS:
            strcpy(request.header, "OPTION");
            break;
        case METHOD_TRACE:
            strcpy(request.header, "TRACE");
            break;
            /* case METHOD_POST: */
            /*     strcpy(request_string, "POST"); */
            /*     break; */
        default:
            assert(0);
    }
    strcat(request.header, " ");

    if (NULL == strstr(url, "://")) {
        fprintf(stderr, "\n%s: is not valid URL.\n", url);
        exit(2);
    }

    if (strlen(url) > 1500) {
        fprintf(stderr, "\nURL is too long.\n");
        exit(2);
    }

    int i = strstr(url,"://") - url+3;

    if (strchr(url+i, '/') == NULL) {
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }

    request.port = 80;
    if(index(url+i,':') != NULL && index(url+i,':') < index(url+i,'/')) {
        char tmp[6];
        strncpy(request.host,url+i,strchr(url+i,':')-url-i);
        memset(tmp, 0, 6);
        strncpy(
                tmp,
                index(url+i,':') + 1,
                strchr(url+i, '/') - index(url+i, ':') - 1
               );
        int port = atoi(tmp);
        if (port) {
            request.port = port;
        }
    } else {
        strncpy(request.host, url+i, strcspn(url+i, "/"));
    }
    // printf("Host=%s\n",host);
    strcat(request.header + strlen(request.header),url+i+strcspn(url+i,"/"));

/*     printf("host:%s\nheader:%s\nurl+i+strcspn(url+i,\"/\"):%s\n", request.host, request.header, url+i+strcspn(url+i,"/")); */
/*     exit(0); */
    /* strcat(request.header, url); */
    strcat(request.header, " HTTP/1.1\r\n");
    if (NULL != global_bench_config.user_agent) {
        strcat(request.header, "User-Agent: ");
        strcat(request.header, global_bench_config.user_agent);
        strcat(request.header, "\r\n");
    } else {
        strcat(request.header, "User-Agent: WebBench "PROGRAM_VERSION"\r\n");
    }
    strcat(request.header, "Host: ");
    strcat(request.header, request.host);
    strcat(request.header, "\r\n");
    strcat(request.header, "Pragma: no-cache\r\n");
    if (global_bench_config.keepalive) {
        strcat(request.header, "Connection: keep-alive\r\n");
    } else {
        strcat(request.header, "Connection: close\r\n");
    }
    strcat(request.header, "\r\n");
    request.header_len = strlen(request.header);
}

static void start_bench()
{
    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    printf("cpu core:%d\n", cpu_count);
    int average_count = 1, ext_count = 0;
    int *ev_count = malloc(sizeof(int)*cpu_count);
    memset(ev_count, 0, sizeof(int)*cpu_count);
    //把每个ev_count平均分摊到进程上面
    if (global_bench_config.client_count > cpu_count) {
        average_count = global_bench_config.client_count/cpu_count;
        ext_count = global_bench_config.client_count%cpu_count;
    }

    for (int i = 0; i < cpu_count; i++) {
        ev_count[i] = average_count;
        if (ext_count--) {
            ev_count[i]++;
        }
    }

    if (pipe(global_bench_config.pipe)) {
        perror("pipe failed.");
        exit(-1);
    }

    pid_t pid;
    int i;
    for (i = 0; i < cpu_count; i++) {
        pid = fork();
        if (pid < (pid_t)0) {
            perror("fork failed.");
            exit(-1);
        } else if(0 == pid) {//child
            break;
        }
    }
    //child
    if (pid == (pid_t)0) {
        //bench
        bench_core(ev_count[i]);
    } else {
        FILE *f;
        f = fdopen(global_bench_config.pipe[0], "r");
        if (NULL == f) {
            perror("open pipe read fail.");
            exit(-1);
        }
        setvbuf(f,NULL,_IONBF,0);

        int i, j, k;
        while (cpu_count--) {
            pid=fscanf(f,"%d %d %d",&i,&j,&k);
            if(pid<2)
            {
                fprintf(stderr,"Some of our childrens died.\n");
                break;
            }
            bench_result.speed+=i;
            bench_result.failed+=j;
            bench_result.bytes+=k;
            /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
        }
        fclose(f);

        printf("\nSpeed=%d pages/s, %d pages/min,%d bytes/sec.\nRequests: %d susceed, %d failed.\n",
                (int)((bench_result.speed + bench_result.failed)/((float)global_bench_config.bench_time)),
                (int)((bench_result.speed + bench_result.failed)/((float)global_bench_config.bench_time/60.0)),
                (int)(bench_result.bytes/(float)global_bench_config.bench_time),
                bench_result.speed,
                bench_result.failed);
    }
}

static void timeout_cb (EV_P_ ev_timer *w, int revents)
{
    is_time_expired = 1;
    ev_break (EV_A_ EVBREAK_ONE);

    FILE *f;
    f = fdopen(global_bench_config.pipe[1], "w");
    if (NULL == f) {
        perror("open pipe write fail.");
        exit(-1);
    }
    fprintf(f, "%d %d %d\n", bench_result.speed, bench_result.failed, bench_result.bytes);
    fclose(f);
    exit(0);
}

static void sock_write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    if (EV_ERROR & revents) {
        printf("error event in read");
        exit(2);
    }

    if (request.header_len != (int)write(watcher->fd, request.header, request.header_len)) {
        bench_result.failed++;
    } else {
        /* bench_result.bytes += request.header_len; */
        bench_result.speed++;
    }

    /* printf("write data:%s", request.header); */
    /* printf("total write:%d\n", request.header_len); */
    ev_io_stop(loop, watcher);
    if (!global_bench_config.keepalive) {
        close(watcher->fd);
        int s = Socket(request.host, request.port);
        ev_io_set(watcher, s, EV_WRITE);
    }
    ev_io_start(loop, watcher);
}

static void sock_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    //printf("call sock cb function,fd:%d\n", watcher->fd);
    if (EV_ERROR & revents) {
        printf("error event");
        exit(2);
    }
    if (EV_WRITE & revents) {
        if (request.header_len != (int)write(watcher->fd, request.header, request.header_len)) {
            bench_result.failed++;
        } else {
            /* bench_result.bytes += request.header_len; */
            /* bench_result.speed++; */
        }

        //printf("write fd:%d\n", watcher->fd);
        ev_io_stop(loop, watcher);
        ev_io_set(watcher, watcher->fd, EV_READ);
        ev_io_start(loop, watcher);
    }

    if (EV_READ & revents) {
        /* printf("fd %d is a read event\n", watcher->fd); */
        int i = read(watcher->fd, tmp_read_buf, 1500);
        if(i<=0) {
            int _err_num = errno;
            printf("Oh dear, something went wrong with read()! %s\n", strerror(_err_num));
            bench_result.failed++;
            ev_io_stop(loop, watcher);
            close(watcher->fd);
            int s = Socket(request.host, request.port);
            ev_io_set(watcher, s, EV_WRITE);
            ev_io_start(loop, watcher);
            return;
        } else {
            bench_result.bytes+=i;
            bench_result.speed++;
        }

        //printf("read fd:%d\n", watcher->fd);
        ev_io_stop(loop, watcher);
        if (!global_bench_config.keepalive) {
            close(watcher->fd);
            int s = Socket(request.host, request.port);
            ev_io_set(watcher, s, EV_WRITE);
        } else {
            ev_io_set(watcher, watcher->fd, EV_WRITE);
        }
        ev_io_start(loop, watcher);
    }
    /* printf("write data:%s", request.header); */
    /* printf("total write:%d\n", request.header_len); */
}

static void bench_core(int event_count)
{
    printf("new process pid:%d,connection count:%d\n", getpid(), event_count);
    struct ev_loop *loop = EV_DEFAULT;
    int sockfd;

    for (int c = 0; c < event_count; c++) {
        if ((sockfd = Socket(request.host, request.port))<0) {
            fprintf(stderr, "\nCreate sock fail.\n");
            exit(2);
        }
        ev_io *conn_writable = malloc(sizeof(ev_io));
        if (global_bench_config.force) {
            ev_io_init(conn_writable, sock_write_cb, sockfd, EV_WRITE);
        } else {
            ev_io_init(conn_writable, sock_cb, sockfd, EV_WRITE);
        }
        ev_io_start(loop, conn_writable);
    }
    //超时设置
    ev_timer_init(&ev_timeout_watcher, timeout_cb, global_bench_config.bench_time, 0.);
    ev_timer_start(loop, &ev_timeout_watcher);

    ev_run (loop, 0);
}
