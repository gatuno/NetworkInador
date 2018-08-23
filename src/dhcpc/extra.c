#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <errno.h>
#include <ctype.h>

#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdarg.h>

#include <sys/ioctl.h>
#include <net/if.h>

#include <time.h>

#include "extra.h"

const int const_int_1 = 1;
const char bb_hexdigits_upcase[] ALIGN1 = "0123456789ABCDEF";

void xpipe(int filedes[2]) {
	if (pipe(filedes)) {
		fprintf (stderr, "can't create pipe");
		exit (EXIT_FAILURE);
	}
}

void bb_signals(int sigs, void (*f)(int)) {
	int sig_no = 0;
	int bit = 1;

	while (sigs) {
		if (sigs & bit) {
			sigs -= bit;
			signal(sig_no, f);
		}
		sig_no++;
		bit <<= 1;
	}
}

void close_on_exec_on(int fd) {
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

/* Turn on nonblocking I/O on a fd */
void ndelay_on(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (flags & O_NONBLOCK)
		return;
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void ndelay_off(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (!(flags & O_NONBLOCK))
		return;
	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

// Die if we can't copy a string to freshly allocated memory.
char* xstrdup(const char *s)
{
	char *t;

	if (s == NULL)
		return NULL;

	t = strdup(s);

	if (t == NULL) {
		fprintf (stderr, "Memory allocation failed");
		exit (EXIT_FAILURE);
	}

	return t;
}

char* hex2bin(char *dst, const char *str, int count)
{
	errno = EINVAL;
	while (*str && count) {
		uint8_t val;
		uint8_t c = *str++;
		if (isdigit(c))
			val = c - '0';
		else if ((c|0x20) >= 'a' && (c|0x20) <= 'f')
			val = (c|0x20) - ('a' - 10);
		else
			return NULL;
		val <<= 4;
		c = *str;
		if (isdigit(c))
			val |= c - '0';
		else if ((c|0x20) >= 'a' && (c|0x20) <= 'f')
			val |= (c|0x20) - ('a' - 10);
		else if (c == ':' || c == '\0')
			val >>= 4;
		else
			return NULL;

		*dst++ = val;
		if (c != '\0')
			str++;
		if (*str == ':')
			str++;
		count--;
	}
	errno = (*str ? ERANGE : 0);
	return dst;
}

#define DIE_ON_ERROR AI_CANONNAME

/* host: "1.2.3.4[:port]", "www.google.com[:port]"
 * port: if neither of above specifies port # */
static len_and_sockaddr* str2sockaddr(
		const char *host, int port,
		sa_family_t af,
		int ai_flags)
{
	int rc;
	len_and_sockaddr *r;
	struct addrinfo *result = NULL;
	struct addrinfo *used_res;
	const char *org_host = host; /* only for error msg */
	const char *cp;
	struct addrinfo hint;

	r = NULL;

	/* Ugly parsing of host:addr */
	if (host[0] == '[') {
		/* Even uglier parsing of [xx]:nn */
		host++;
		cp = strchr(host, ']');
		if (!cp || (cp[1] != ':' && cp[1] != '\0')) {
			/* Malformed: must be [xx]:nn or [xx] */
			fprintf(stderr, "bad address '%s'", org_host);
			if (ai_flags & DIE_ON_ERROR) {
				exit (EXIT_FAILURE);
			}
			return NULL;
		}
	} else {
		cp = strrchr(host, ':');
		if (cp && strchr(host, ':') != cp) {
			/* There is more than one ':' (e.g. "::1") */
			cp = NULL; /* it's not a port spec */
		}
	}
	if (cp) { /* points to ":" or "]:" */
		int sz = cp - host + 1;

		host = safe_strncpy(alloca(sz), host, sz);
		if (*cp != ':') {
			cp++; /* skip ']' */
			if (*cp == '\0') /* [xx] without port */
				goto skip;
		}
		cp++; /* skip ':' */
		port = bb_strtou(cp, NULL, 10);
		if (errno || (unsigned)port > 0xffff) {
			fprintf(stderr, "bad port spec '%s'", org_host);
			if (ai_flags & DIE_ON_ERROR) {
				exit (EXIT_FAILURE);
			}
			return NULL;
		}
 skip: ;
	}

	/* Next two if blocks allow to skip getaddrinfo()
	 * in case host name is a numeric IP(v6) address.
	 * getaddrinfo() initializes DNS resolution machinery,
	 * scans network config and such - tens of syscalls.
	 */
	/* If we were not asked specifically for IPv6,
	 * check whether this is a numeric IPv4 */
	if(af != AF_INET6) {
		struct in_addr in4;
		if (inet_aton(host, &in4) != 0) {
			r = xzalloc(LSA_LEN_SIZE + sizeof(struct sockaddr_in));
			r->len = sizeof(struct sockaddr_in);
			r->u.sa.sa_family = AF_INET;
			r->u.sin.sin_addr = in4;
			goto set_port;
		}
	}
	/* If we were not asked specifically for IPv4,
	 * check whether this is a numeric IPv6 */
	if (af != AF_INET) {
		struct in6_addr in6;
		if (inet_pton(AF_INET6, host, &in6) > 0) {
			r = xzalloc(LSA_LEN_SIZE + sizeof(struct sockaddr_in6));
			r->len = sizeof(struct sockaddr_in6);
			r->u.sa.sa_family = AF_INET6;
			r->u.sin6.sin6_addr = in6;
			goto set_port;
		}
	}

	memset(&hint, 0 , sizeof(hint));
	hint.ai_family = af;
	/* Need SOCK_STREAM, or else we get each address thrice (or more)
	 * for each possible socket type (tcp,udp,raw...): */
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_flags = ai_flags & ~DIE_ON_ERROR;
	rc = getaddrinfo(host, NULL, &hint, &result);
	if (rc || !result) {
		fprintf (stderr, "bad address '%s'", org_host);
		if (ai_flags & DIE_ON_ERROR) {
			exit (EXIT_FAILURE);
		}
		goto ret;
	}
	used_res = result;
	r = xmalloc(LSA_LEN_SIZE + used_res->ai_addrlen);
	r->len = used_res->ai_addrlen;
	memcpy(&r->u.sa, used_res->ai_addr, used_res->ai_addrlen);

 set_port:
	set_nport(&r->u.sa, htons(port));
 ret:
	if (result)
		freeaddrinfo(result);
	return r;
}

len_and_sockaddr* host2sockaddr(const char *host, int port) {
	return str2sockaddr(host, port, AF_UNSPEC, 0);
}

len_and_sockaddr* xhost2sockaddr(const char *host, int port) {
	return str2sockaddr(host, port, AF_UNSPEC, DIE_ON_ERROR);
}

void* xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr == NULL && size != 0) {
		fprintf (stderr, "Memory allocation failed");
		exit (EXIT_FAILURE);
	}
	return ptr;
}

void set_nport(struct sockaddr *sa, unsigned port)
{
	if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (void*) sa;
		sin6->sin6_port = port;
		return;
	}
	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (void*) sa;
		sin->sin_port = port;
		return;
	}
	/* What? UNIX socket? IPX?? :) */
}

int xsocket(int domain, int type, int protocol) {
	int r = socket(domain, type, protocol);

	if (r < 0) {
		/* Hijack vaguely related config option */
		const char *s = "INET";
# ifdef AF_PACKET
		if (domain == AF_PACKET) s = "PACKET";
# endif
# ifdef AF_NETLINK
		if (domain == AF_NETLINK) s = "NETLINK";
# endif
		if (domain == AF_INET6) s = "INET6";
		
		fprintf (stderr, "Error socket (AF_%s,%d,%d)", s, type, protocol);
		exit (EXIT_FAILURE);
	}

	return r;
}

// Die with an error message if we can't bind a socket to an address.
void xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen) {
	if (bind(sockfd, my_addr, addrlen)) {
		fprintf (stderr, "Error: bind");
		exit (EXIT_FAILURE);
	}
}

int ioctl_or_perror(int fd, unsigned request, void *argp) {
	va_list p;
	int ret = ioctl(fd, request, argp);

	if (ret < 0) {
		perror ("Error ioctl");
	}
	return ret;
}

/* Like strncpy but make sure the resulting string is always 0 terminated. */
char* safe_strncpy(char *dst, const char *src, size_t size) {
	if (!size) return dst;
	dst[--size] = '\0';
	return strncpy(dst, src, size);
}

/* Like strcpy but can copy overlapping strings. */
void overlapping_strcpy(char *dst, const char *src) {
	/* Cheap optimization for dst == src case -
	 * better to have it here than in many callers.
	 */
	if (dst != src) {
		while ((*dst = *src) != '\0') {
			dst++;
			src++;
		}
	}
}

// Die if we can't allocate and zero size bytes of memory.
void* xzalloc(size_t size) {
	void *ptr = xmalloc(size);
	memset(ptr, 0, size);
	return ptr;
}

void* xrealloc(void *ptr, size_t size) {
	ptr = realloc(ptr, size);
	if (ptr == NULL && size != 0) {
		fprintf (stderr, "Memory allocation failed");
		exit (EXIT_FAILURE);
	}
	return ptr;
}

ssize_t safe_read(int fd, void *buf, size_t count) {
	ssize_t n;

	do {
		n = read(fd, buf, count);
	} while (n < 0 && errno == EINTR);

	return n;
}

uint16_t inet_cksum(uint16_t *addr, int nleft) {
	/*
	 * Our algorithm is simple, using a 32 bit accumulator,
	 * we add sequential 16 bit words to it, and at the end, fold
	 * back all the carry bits from the top 16 bits into the lower
	 * 16 bits.
	 */
	unsigned sum = 0;
	while (nleft > 1) {
		sum += *addr++;
		nleft -= 2;
	}

	/* Mop up an odd byte, if necessary */
	if (nleft == 1) {
		if (BB_LITTLE_ENDIAN)
			sum += *(uint8_t*)addr;
		else
			sum += *(uint8_t*)addr << 8;
	}

	/* Add back carry outs from top 16 bits to low 16 bits */
	sum = (sum >> 16) + (sum & 0xffff);     /* add hi 16 to low 16 */
	sum += (sum >> 16);                     /* add carry */

	return (uint16_t)~sum;
}

/* Emit a string of hex representation of bytes */
char* bin2hex(char *p, const char *cp, int count) {
	while (count) {
		unsigned char c = *cp++;
		/* put lowercase hex digits */
		*p++ = 0x20 | bb_hexdigits_upcase[c >> 4];
		*p++ = 0x20 | bb_hexdigits_upcase[c & 0xf];
		count--;
	}
	return p;
}

char* xasprintf(const char *format, ...) {
	va_list p;
	int r;
	char *string_ptr;

	va_start(p, format);
	r = vasprintf(&string_ptr, format, p);
	va_end(p);

	if (r < 0) {
		fprintf (stderr, "Memory allocation failed");
		exit (EXIT_FAILURE);
	}
	return string_ptr;
}

static unsigned long long ret_ERANGE(void)
{
	errno = ERANGE; /* this ain't as small as it looks (on glibc) */
	return ULLONG_MAX;
}

static unsigned long long handle_errors(unsigned long long v, char **endp)
{
	char next_ch = **endp;

	/* errno is already set to ERANGE by strtoXXX if value overflowed */
	if (next_ch) {
		/* "1234abcg" or out-of-range? */
		if (isalnum(next_ch) || errno)
			return ret_ERANGE();
		/* good number, just suspicious terminator */
		errno = EINVAL;
	}
	return v;
}

unsigned bb_strtou(const char *arg, char **endp, int base) {
	unsigned long v;
	char *endptr;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	if (!isalnum(arg[0])) return ret_ERANGE();
	errno = 0;
	v = strtoul(arg, endp, base);
	if (v > UINT_MAX) return ret_ERANGE();
	return handle_errors(v, endp);
}

char* strncpy_IFNAMSIZ(char *dst, const char *src) {
#ifndef IFNAMSIZ
	enum { IFNAMSIZ = 16 };
#endif
	return strncpy(dst, src, IFNAMSIZ);
}

void setsockopt_reuseaddr(int fd) {
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &const_int_1, sizeof(const_int_1));
}

int setsockopt_broadcast(int fd) {
	return setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &const_int_1, sizeof(const_int_1));
}

#ifdef SO_BINDTODEVICE
int setsockopt_bindtodevice(int fd, const char *iface) {
	int r;
	struct ifreq ifr;
	strncpy_IFNAMSIZ(ifr.ifr_name, iface);
	/* NB: passing (iface, strlen(iface) + 1) does not work!
	 * (maybe it works on _some_ kernels, but not on 2.6.26)
	 * Actually, ifr_name is at offset 0, and in practice
	 * just giving char[IFNAMSIZ] instead of struct ifreq works too.
	 * But just in case it's not true on some obscure arch... */
	r = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
	if (r)
		fprintf (stderr, "can't bind to interface %s", iface);
	return r;
}
#else
int setsockopt_bindtodevice(int fd, const char *iface) {
	fprintf (stderr, "SO_BINDTODEVICE is not supported on this system");
	return -1;
}
#endif

unsigned monotonic_sec(void) {
	return time(NULL);
}

ssize_t safe_write(int fd, const void *buf, size_t count) {
	ssize_t n;

	do {
		n = write(fd, buf, count);
	} while (n < 0 && errno == EINTR);

	return n;
}

