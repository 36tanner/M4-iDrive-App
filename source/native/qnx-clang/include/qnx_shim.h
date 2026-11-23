/* Clean-room QNX Neutrino 6.5 (armle-v7) declarations for building the ID6 workers with clang.
 * Prototypes follow POSIX; values follow QNX 6.5 and are checked against the stock v0.7.6 binaries'
 * compiled call sites (see verify notes in qnxbuild/README). Only what the workers use. */
#ifndef QNX_SHIM_H
#define QNX_SHIM_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>

typedef int ssize_t;
typedef int pid_t;
typedef unsigned uid_t, gid_t, mode_t, socklen_t, useconds_t;
typedef long off_t;
typedef long time_t;
typedef int clockid_t;
typedef unsigned char sa_family_t;
typedef unsigned short in_port_t;
typedef unsigned in_addr_t;
typedef unsigned char u_char; typedef unsigned short u_short; typedef unsigned u_int; typedef unsigned long u_long;

/* errno: QNX 6.5 <errno.h> */
extern int *__get_errno_ptr(void);
#define errno (*__get_errno_ptr())
#define EOK 0
#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EINVAL 22
#define ENOSPC 28
#define EPIPE 32
#define ERANGE 34
#define ENOSYS 89
#define ETIME 62
#define EINPROGRESS 236
#define EALREADY 237
#define ENOTSOCK 238
#define EMSGSIZE 240
#define EADDRINUSE 248
#define EADDRNOTAVAIL 249
#define ENETDOWN 250
#define ENETUNREACH 251
#define ECONNABORTED 253
#define ECONNRESET 254
#define ENOBUFS 255
#define EISCONN 256
#define ENOTCONN 257
#define ETIMEDOUT 260
#define ECONNREFUSED 261
#define EHOSTDOWN 264
#define EHOSTUNREACH 265

/* stdio: Dinkum FILE is 80 bytes; stderr is the exported _Stderr object. */
typedef struct _Filet { unsigned char _opaque[80]; } FILE;
extern FILE _Stdin, _Stdout, _Stderr;
#define stdin (&_Stdin)
#define stdout (&_Stdout)
#define stderr (&_Stderr)
#define EOF (-1)
#define BUFSIZ 1024
int printf(const char *, ...); int fprintf(FILE *, const char *, ...); int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...); int vsnprintf(char *, size_t, const char *, va_list);
int vfprintf(FILE *, const char *, va_list); int puts(const char *); int fputs(const char *, FILE *);
int fputc(int, FILE *); int putchar(int); size_t fwrite(const void *, size_t, size_t, FILE *);
size_t fread(void *, size_t, size_t, FILE *); int fflush(FILE *); void perror(const char *);
FILE *fopen(const char *, const char *); int fclose(FILE *); int fscanf(FILE *, const char *, ...);
int sscanf(const char *, const char *, ...); char *fgets(char *, int, FILE *);

/* stdlib / string */
void exit(int) __attribute__((noreturn)); void _exit(int) __attribute__((noreturn));
int atexit(void (*)(void)); void abort(void) __attribute__((noreturn));
void *malloc(size_t); void *calloc(size_t, size_t); void *realloc(void *, size_t); void free(void *);
long strtol(const char *, char **, int); unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int); unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **); int atoi(const char *); char *getenv(const char *); int abs(int);
void *memcpy(void *, const void *, size_t); void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t); int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t); size_t strlen(const char *); int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t); char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t); char *strchr(const char *, int); char *strrchr(const char *, int);
char *strstr(const char *, const char *); char *strerror(int); char *strcat(char *, const char *);

/* ctype: C locale, inline so nothing depends on the _Ctype table layout */
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isalpha(int c) { return isupper(c) || islower(c); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isprint(int c) { return c >= 0x20 && c < 0x7f; }
static inline int toupper(int c) { return islower(c) ? c - 32 : c; }
static inline int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* math: Dinkum libm; sin/cos go through _Sin like the stock build */
double round(double); double floor(double); double ceil(double); double sqrt(double);
double pow(double, double); double log10(double); double exp(double); double atan2(double, double);
double _Sin(double, unsigned int);
static inline double sin(double x) { return _Sin(x, 0); }
static inline double cos(double x) { return _Sin(x, 1); }
#define fabs(x) __builtin_fabs(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define NAN __builtin_nan("")
#define INFINITY __builtin_inf()
#define HUGE_VAL __builtin_huge_val()
#define M_PI 3.14159265358979323846

/* time */
struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; long tv_usec; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 2
int clock_gettime(clockid_t, struct timespec *); int nanosleep(const struct timespec *, struct timespec *);
time_t time(time_t *); char *ctime(const time_t *);

/* unistd / fcntl */
int close(int); ssize_t read(int, void *, size_t); ssize_t write(int, const void *, size_t);
pid_t getpid(void); pid_t getppid(void); int access(const char *, int); unsigned alarm(unsigned);
unsigned sleep(unsigned); int usleep(useconds_t); int unlink(const char *); int dup2(int, int);
int pipe(int[2]); int isatty(int);
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
int open(const char *, int, ...); int fcntl(int, int, ...);
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_APPEND 0x0008
#define O_NONBLOCK 0x0080
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
#define O_EXCL 0x0400
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1

/* signals */
typedef struct { long __bits[2]; } sigset_t;
typedef int sig_atomic_t;
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGABRT 6
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGUSR1 16
#define SIGUSR2 17
#define SIGCHLD 18
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
struct sigaction { void (*sa_handler)(int); int sa_flags; sigset_t sa_mask; };
int sigaction(int, const struct sigaction *, struct sigaction *); void (*signal(int, void (*)(int)))(int);
int sigemptyset(sigset_t *); int sigfillset(sigset_t *); int sigaddset(sigset_t *, int);
int sigprocmask(int, const sigset_t *, sigset_t *); int kill(pid_t, int); int raise(int);

/* pthreads: sizes are at least QNX's; objects are only ever initialised by libc */
typedef int pthread_t;
typedef struct { int __count; unsigned __owner; } pthread_mutex_t;
typedef struct { int __count; unsigned __owner; } pthread_cond_t;
typedef struct { int __words[8]; } pthread_mutexattr_t, pthread_condattr_t;
typedef struct { int __words[16]; } pthread_attr_t;
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **); int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_lock(pthread_mutex_t *); int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutex_destroy(pthread_mutex_t *); int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *); int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_cond_broadcast(pthread_cond_t *); int pthread_cond_signal(pthread_cond_t *);
int pthread_condattr_init(pthread_condattr_t *); int pthread_condattr_destroy(pthread_condattr_t *);
int pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int pthread_sigmask(int, const sigset_t *, sigset_t *);

/* dlfcn */
#define RTLD_LAZY 0x0001
#define RTLD_NOW 0x0002
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL 0x0200
void *dlopen(const char *, int); void *dlsym(void *, const char *); int dlclose(void *); char *dlerror(void);

/* poll */
struct pollfd { int fd; short events; short revents; };
typedef unsigned nfds_t;
#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
int poll(struct pollfd *, nfds_t, int);

/* sockets: io-pkt, NetBSD-derived (BSD sockaddr with a length byte) */
struct sockaddr { unsigned char sa_len; sa_family_t sa_family; char sa_data[14]; };
struct in_addr { in_addr_t s_addr; };
struct sockaddr_in { unsigned char sin_len; sa_family_t sin_family; in_port_t sin_port;
                     struct in_addr sin_addr; char sin_zero[8]; };
#define AF_UNSPEC 0
#define AF_INET 2
#define PF_INET AF_INET
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_DONTROUTE 0x0010
#define SO_BROADCAST 0x0020
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define MSG_PEEK 0x2
#define MSG_DONTWAIT 0x80
#define MSG_NOSIGNAL 0x800
#define SHUT_RDWR 2
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define TCP_NODELAY 1
#define INADDR_ANY ((in_addr_t)0)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INET_ADDRSTRLEN 16
int socket(int, int, int); int bind(int, const struct sockaddr *, socklen_t);
int connect(int, const struct sockaddr *, socklen_t); int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *); int shutdown(int, int);
ssize_t send(int, const void *, size_t, int); ssize_t recv(int, void *, size_t, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
int setsockopt(int, int, int, const void *, socklen_t); int getsockopt(int, int, int, void *, socklen_t *);
int inet_pton(int, const char *, void *); const char *inet_ntop(int, const void *, char *, socklen_t);
static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }

/* sys/stat, statvfs (M4 logger) */
struct stat { unsigned char __opaque[128]; };   /* filled in once the layout is checked */
int mkdir(const char *, mode_t);
#endif
