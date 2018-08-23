#ifndef __EXTRA_H__
#define __EXTRA_H__ 1

#include <netinet/in.h>
#include <stddef.h>

/* In this form code with pipes is much more readable */
struct fd_pair { int rd; int wr; };
#define piped_pair(pair)  pipe(&((pair).rd))
#define xpiped_pair(pair) xpipe(&((pair).rd))

#define ALIGN1 __attribute__((aligned(1)))
#define ALIGN2 __attribute__((aligned(2)))
#define PACKED __attribute__ ((__packed__))
#define ALIGNED(m) __attribute__ ((__aligned__(m)))
#define NOINLINE      __attribute__((__noinline__))

#define CONFIG_UDHCPC_SLACK_FOR_BUGGY_SERVERS 80

#define host_and_af2sockaddr(host, port, af) host2sockaddr((host), (port))
#define xhost_and_af2sockaddr(host, port, af) xhost2sockaddr((host), (port))

/* ---- Endian Detection ------------------------------------ */

#include <limits.h>
#if defined(__digital__) && defined(__unix__)
# include <sex.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
   || defined(__APPLE__)
# include <sys/resource.h>  /* rlimit */
# include <machine/endian.h>
# define bswap_64 __bswap64
# define bswap_32 __bswap32
# define bswap_16 __bswap16
#else
# include <byteswap.h>
# include <endian.h>
#endif

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
# define BB_BIG_ENDIAN 1
# define BB_LITTLE_ENDIAN 0
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN
# define BB_BIG_ENDIAN 0
# define BB_LITTLE_ENDIAN 1
#elif defined(_BYTE_ORDER) && _BYTE_ORDER == _BIG_ENDIAN
# define BB_BIG_ENDIAN 1
# define BB_LITTLE_ENDIAN 0
#elif defined(_BYTE_ORDER) && _BYTE_ORDER == _LITTLE_ENDIAN
# define BB_BIG_ENDIAN 0
# define BB_LITTLE_ENDIAN 1
#elif defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN
# define BB_BIG_ENDIAN 1
# define BB_LITTLE_ENDIAN 0
#elif defined(BYTE_ORDER) && BYTE_ORDER == LITTLE_ENDIAN
# define BB_BIG_ENDIAN 0
# define BB_LITTLE_ENDIAN 1
#elif defined(__386__)
# define BB_BIG_ENDIAN 0
# define BB_LITTLE_ENDIAN 1
#else
# error "Can't determine endianness"
#endif

#if ULONG_MAX > 0xffffffff
# define bb_bswap_64(x) bswap_64(x)
#endif

/* SWAP_LEnn means "convert CPU<->little_endian by swapping bytes" */
#if BB_BIG_ENDIAN
# define SWAP_BE16(x) (x)
# define SWAP_BE32(x) (x)
# define SWAP_BE64(x) (x)
# define SWAP_LE16(x) bswap_16(x)
# define SWAP_LE32(x) bswap_32(x)
# define SWAP_LE64(x) bb_bswap_64(x)
# define IF_BIG_ENDIAN(...) __VA_ARGS__
# define IF_LITTLE_ENDIAN(...)
#else
# define SWAP_BE16(x) bswap_16(x)
# define SWAP_BE32(x) bswap_32(x)
# define SWAP_BE64(x) bb_bswap_64(x)
# define SWAP_LE16(x) (x)
# define SWAP_LE32(x) (x)
# define SWAP_LE64(x) (x)
# define IF_BIG_ENDIAN(...)
# define IF_LITTLE_ENDIAN(...) __VA_ARGS__
#endif

/* At 4.4 gcc become much more anal about this, need to use "aliased" types */
#if __GNUC_PREREQ(4,4)
# define FIX_ALIASING __attribute__((__may_alias__))
#else
# define FIX_ALIASING
#endif

/* ---- Unaligned access ------------------------------------ */

#include <stdint.h>
typedef int      bb__aliased_int      FIX_ALIASING;
typedef long     bb__aliased_long     FIX_ALIASING;
typedef uint16_t bb__aliased_uint16_t FIX_ALIASING;
typedef uint32_t bb__aliased_uint32_t FIX_ALIASING;
typedef uint64_t bb__aliased_uint64_t FIX_ALIASING;

/* NB: unaligned parameter should be a pointer, aligned one -
 * a lvalue. This makes it more likely to not swap them by mistake
 */
#if defined(i386) || defined(__x86_64__) || defined(__powerpc__)
# define move_from_unaligned_int(v, intp)  ((v) = *(bb__aliased_int*)(intp))
# define move_from_unaligned_long(v, longp) ((v) = *(bb__aliased_long*)(longp))
# define move_from_unaligned16(v, u16p) ((v) = *(bb__aliased_uint16_t*)(u16p))
# define move_from_unaligned32(v, u32p) ((v) = *(bb__aliased_uint32_t*)(u32p))
# define move_to_unaligned16(u16p, v)   (*(bb__aliased_uint16_t*)(u16p) = (v))
# define move_to_unaligned32(u32p, v)   (*(bb__aliased_uint32_t*)(u32p) = (v))
/* #elif ... - add your favorite arch today! */
#else
/* performs reasonably well (gcc usually inlines memcpy here) */
# define move_from_unaligned_int(v, intp) (memcpy(&(v), (intp), sizeof(int)))
# define move_from_unaligned_long(v, longp) (memcpy(&(v), (longp), sizeof(long)))
# define move_from_unaligned16(v, u16p) (memcpy(&(v), (u16p), 2))
# define move_from_unaligned32(v, u32p) (memcpy(&(v), (u32p), 4))
# define move_to_unaligned16(u16p, v) do { \
	uint16_t __t = (v); \
	memcpy((u16p), &__t, 2); \
} while (0)
# define move_to_unaligned32(u32p, v) do { \
	uint32_t __t = (v); \
	memcpy((u32p), &__t, 4); \
} while (0)
#endif

#if defined(i386) || defined(__x86_64__) || defined(__mips__) || defined(__cris__)
/* add other arches which benefit from this... */
typedef signed char smallint;
typedef unsigned char smalluint;
#else
/* for arches where byte accesses generate larger code: */
typedef int smallint;
typedef unsigned smalluint;
#endif

extern const int const_int_1;

typedef struct len_and_sockaddr {
	socklen_t len;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} u;
} len_and_sockaddr;

enum {
	LSA_LEN_SIZE = offsetof(len_and_sockaddr, u),
	LSA_SIZEOF_SA = sizeof(
		union {
			struct sockaddr sa;
			struct sockaddr_in sin;
			struct sockaddr_in6 sin6;
		}
	)
};

void bb_signals(int sigs, void (*f)(int));
void close_on_exec_on(int fd);
void xpipe(int filedes[2]);
void ndelay_on(int fd);
void ndelay_off(int fd);
char* xstrdup(const char *s);
char* hex2bin(char *dst, const char *str, int count);
char* bin2hex(char *p, const char *cp, int count);

len_and_sockaddr* host2sockaddr(const char *host, int port);
len_and_sockaddr* xhost2sockaddr(const char *host, int port);
void* xmalloc(size_t size);
void set_nport(struct sockaddr *sa, unsigned port);
int xsocket(int domain, int type, int protocol);
void xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen);
int ioctl_or_perror(int fd, unsigned request, void *argp);
char* safe_strncpy(char *dst, const char *src, size_t size);
void overlapping_strcpy(char *dst, const char *src);
void* xzalloc(size_t size);
void* xrealloc(void *ptr, size_t size);
ssize_t safe_read(int fd, void *buf, size_t count);
ssize_t safe_write(int fd, const void *buf, size_t count);

uint16_t inet_cksum(uint16_t *addr, int nleft);
char* xasprintf(const char *format, ...);

unsigned bb_strtou(const char *arg, char **endp, int base);
char* strncpy_IFNAMSIZ(char *dst, const char *src);
void setsockopt_reuseaddr(int fd);
int setsockopt_broadcast(int fd);
int setsockopt_bindtodevice(int fd, const char *iface);
unsigned monotonic_sec(void);

#endif
