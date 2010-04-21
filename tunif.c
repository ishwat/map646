/*
 * Copyright 2010 IIJ Innovation Institute Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY IIJ INNOVATION INSTITUTE INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL IIJ INNOVATION INSTITUTE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#include <err.h>

#if !defined(__linux__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#endif
#include <sys/ioctl.h>

#include <net/if.h>
#if defined(__linux__)
#include <linux/if_tun.h>
#else
#include <ifaddrs.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/if_tun.h>
#endif
#include <netinet/in.h>

union sockunion {
  struct sockaddr sa;
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
#if !defined(__linux__)
  struct sockaddr_dl sdl;
#endif
};

static int tun_make_netmask(union sockunion *, int, int);

char tun_if_name[IFNAMSIZ];

/*
 * Create a new tun interface with the given name.  If the name
 * exists, just return an error.
 *
 * The created tun interface doesn't have the NO_PI flag (in Linux),
 * and has the TUNSIFHEAD flag (in BSD) to provide address family
 * information at the beginning of all incoming/outgoing packets.
 */
int
tun_alloc(char *tun_if_name)
{
  assert(tun_if_name != NULL);

  int udp_fd;
  udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_fd == -1) {
    err(EXIT_FAILURE, "failed to open control socket for tun creation.");
  }

#if defined(__linux__)
  /*
   * Create a new tun device.
   */
  int tun_fd;
  tun_fd = open("/dev/net/tun", O_RDWR);
  if (tun_fd == -1) {
    err(EXIT_FAILURE, "cannot create a control channel of the tun interface.");
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  ifr.ifr_flags = IFF_TUN;
  strncpy(ifr.ifr_name, tun_if_name, IFNAMSIZ);
  if (ioctl(tun_fd, TUNSETIFF, (void *)&ifr) == -1) {
    close(tun_fd);
    err(EXIT_FAILURE, "cannot create a tun interface %s.\n", tun_if_name);
  }
  strncpy(tun_if_name, ifr.ifr_name, IFNAMSIZ);
#else
  /*
   * Create a new tun device.
   */
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy(ifr.ifr_name, tun_if_name, IFNAMSIZ);
  if (ioctl(udp_fd, SIOCIFCREATE2, &ifr) == -1) {
    err(EXIT_FAILURE, "cannot create %s interface.", ifr.ifr_name);
  }
  strncpy(tun_if_name, ifr.ifr_name, IFNAMSIZ);

  char tun_dev_name[MAXPATHLEN];
  strcat(tun_dev_name, "/dev/");
  strcat(tun_dev_name, ifr.ifr_name);

  int tun_fd;
  tun_fd = open(tun_dev_name, O_RDWR);
  if (tun_fd == -1) {
    err(EXIT_FAILURE, "cannot open a tun device %s.", tun_dev_name);
  }
  int tun_iff_mode = IFF_POINTOPOINT;
  if (ioctl(tun_fd, TUNSIFMODE, &tun_iff_mode) == -1) {
    err(EXIT_FAILURE, "failed to set TUNSIFMODE to %x.\n", tun_iff_mode);
  }
  /*
   * TUNSIFHEAD enables the address family information prepending
   * procedure before the actual packets.
   */
  int on = 1;
  if (ioctl(tun_fd, TUNSIFHEAD, &on) == -1) {
    err(EXIT_FAILURE, "failed to set TUNSIFHEAD to %d.\n", on);
  }
#endif

  /*
   * Make the tun device up.
   */
  memset(&ifr, 0, sizeof(struct ifreq));
  ifr.ifr_flags = IFF_UP;
  strncpy(ifr.ifr_name, tun_if_name, IFNAMSIZ);
  if (ioctl(udp_fd, SIOCSIFFLAGS, (void *)&ifr) == -1) {
    err(EXIT_FAILURE, "failed to make %s up.", tun_if_name);
  }

  close(udp_fd);

  return (tun_fd);
}

/*
 * Delete the tun interface created at launch time.  This code is
 * required only for BSD operating system.  In Linux systems, the tun
 * interface is deleted automatically when the process that created
 * the tun interface exits.
 */
#if !defined(__linux__)
int
tun_dealloc(const char *tun_if_name)
{
  int udp_fd;
  udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_fd == -1) {
    warn("failed to open control socket for tun deletion.");
    return (-1);
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy(ifr.ifr_name, tun_if_name, IFNAMSIZ);
  if (ioctl(udp_fd, SIOCIFDESTROY, &ifr) == -1) {
    warn("cannot destroy %s interface.", ifr.ifr_name);
    close(udp_fd);
    return (-1);
  }

  close(udp_fd);

  return (0);
}
#endif

/*
 * Get the address family information from the head of the packet.
 * The buf pointer must point the head of the packet, and the buffer
 * must be longer than 4 bytes.
 *
 * In BSD systems, the address family information is stored in
 * uint32_t type at the beginning of a packet.  In Linux systems,
 * struct tun_pi{} is prepended instead.  The proto member variable
 * includes the Ether frame type of the contents.
 */
uint32_t
tun_get_af(const void *buf)
{
  uint32_t af = 255; /* XXX */

#if defined(__linux__)
  struct tun_pi *pi = (struct tun_pi *)buf;
  int ether_type = ntohs(pi->proto);
  switch (ether_type) {
  case ETH_P_IP:
    af = AF_INET;
    break;
  case ETH_P_IPV6:
    af = AF_INET6;
    break;
  default:
    warnx("unknown ether frame type %x received.", ether_type);
    break;
  }
#else
  af = ntohl(*(uint32_t *)buf);
#endif

  return (af);
}

/*
 * Set the address family information specified as the af argument.
 * The buf pointer must be longer than 4 bytes.  For the format of the
 * contents, please refer the tun_get_af() function.
 */
int
tun_set_af(void *buf, uint32_t af)
{
#if defined(__linux__)
  uint16_t ether_type;

  switch(af) {
  case AF_INET:
    ether_type = ETH_P_IP;
    break;
  case AF_INET6:
    ether_type = ETH_P_IPV6;
    break;
  default:
    warnx("unsupported address family %d", af);
    return (-1);
  }

  struct tun_pi *pi = buf;
  pi->flags = 0;
  pi->proto = htons(ether_type);

  return (0);
#else
  uint32_t *af_space = buf;

  *af_space = htonl(af);

  return (0);
#endif
}

int
tun_route_add(int af, const void *addr, int prefix_len)
{
  assert(addr != NULL);
  assert(prefix_len >= 0);

#if defined(__linux__)
  /* XXX not implemented yet */
  warnx("built-in route manipulation is not supported yet.");
  return (0);
#else
  int rtm_addrs = 0;
  int rtm_flags = RTF_UP|RTF_HOST|RTF_STATIC;
  union sockunion so_dst, so_gate, so_mask;

  switch (af) {
  case AF_INET:
    /* Prepare destination address information. */
    memset(&so_dst.sin, 0, sizeof(struct sockaddr_in));
    so_dst.sin.sin_len = sizeof(struct sockaddr_in);
    so_dst.sin.sin_family = AF_INET;
    memcpy(&so_dst.sin.sin_addr, addr, sizeof(struct in_addr));
    rtm_addrs |= RTA_DST;

    /* Create netmask information if specified. */
    if (prefix_len < 32) {
      memset(&so_mask.sin, 0, sizeof(struct sockaddr_in));
      so_mask.sin.sin_len = sizeof(struct sockaddr_in);
      so_mask.sin.sin_family = AF_INET;
      tun_make_netmask(&so_mask, AF_INET, prefix_len);
      rtm_addrs |= RTA_NETMASK;
      rtm_flags &= ~RTF_HOST;
    }
    break;

  case AF_INET6:
    /* Prepare destination address information. */
    memset(&so_dst.sin6, 0, sizeof(struct sockaddr_in6));
    so_dst.sin6.sin6_len = sizeof(struct sockaddr_in6);
    so_dst.sin6.sin6_family = AF_INET6;
    memcpy(&so_dst.sin6.sin6_addr, addr, sizeof(struct in6_addr));
    rtm_addrs |= RTA_DST;

    /* Create netmask information if specified. */
    if (prefix_len < 128) {
      memset(&so_mask.sin6, 0, sizeof(struct sockaddr_in6));
      so_mask.sin6.sin6_len = sizeof(struct sockaddr_in6);
      so_mask.sin6.sin6_family = AF_INET6;
      tun_make_netmask(&so_mask, AF_INET6, prefix_len);
      rtm_addrs |= RTA_NETMASK;
      rtm_flags &= ~RTF_HOST;
    }
    break;

  default:
    warnx("unsupported address family %d", af);
    return (-1);
  }

  struct ifaddrs *ifap, *ifa;
  struct sockaddr_dl *sdlp = NULL;
  if (getifaddrs(&ifap)) {
    err(EXIT_FAILURE, "cannot get ifaddrs.");
  }
  for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family != AF_LINK)
      continue;
    if (strcmp(tun_if_name, ifa->ifa_name))
      continue;
    sdlp = (struct sockaddr_dl *)ifa->ifa_addr;
  }
  memcpy(&so_gate.sdl, sdlp, sdlp->sdl_len);
  freeifaddrs(ifap);
  if (sdlp == NULL) {
    errx(EXIT_FAILURE, "cannot find a link-layer address of %s.", tun_if_name);
  }
  rtm_addrs |= RTA_GATEWAY;

  struct {
    struct rt_msghdr m_rtm;
    char m_space[512];
  } m_rtmsg;
  char *cp = m_rtmsg.m_space;
  int l;
  static int seq = 0;
  memset(&m_rtmsg, 0, sizeof(m_rtmsg));
  m_rtmsg.m_rtm.rtm_type = RTM_ADD;
  m_rtmsg.m_rtm.rtm_flags = rtm_flags;
  m_rtmsg.m_rtm.rtm_version = RTM_VERSION;
  m_rtmsg.m_rtm.rtm_seq = ++seq;
  m_rtmsg.m_rtm.rtm_addrs = rtm_addrs;
#define NEXTADDR(w, u) \
  if (rtm_addrs & (w)) { \
    l = SA_SIZE(&(u.sa)); memmove(cp, &(u), l); cp += l; \
  }
  NEXTADDR(RTA_DST, so_dst);
  NEXTADDR(RTA_GATEWAY, so_gate);
  NEXTADDR(RTA_NETMASK, so_mask);
  m_rtmsg.m_rtm.rtm_msglen = l = cp - (char *)&m_rtmsg;

  int route_fd;
  ssize_t write_len;
  route_fd = socket(PF_ROUTE, SOCK_RAW, 0);
  if (route_fd == -1) {
    err(EXIT_FAILURE, "failed to open a routing socket.");
  }
  write_len = write(route_fd, (char *)&m_rtmsg, l);
  if (write_len == -1) {
    err(EXIT_FAILURE, "failed to install route information.");
  }
  close(route_fd);

  return (0);
#endif
}

static int
tun_make_netmask(union sockunion *mask, int af, int prefix_len)
{
  assert(mask != NULL);
  assert(prefix_len > 0);

  int max, q, r, sa_len;
  char *p;

  switch (af) {
  case AF_INET:
    max = 32;
    sa_len = sizeof(struct sockaddr_in);
    p = (char *)&mask->sin.sin_addr;
    break;

  case AF_INET6:
    max = 128;
    sa_len = sizeof(struct sockaddr_in6);
    p = (char *)&mask->sin6.sin6_addr;
    break;

  default:
    errx(EXIT_FAILURE, "unsupported address family %d.", af);
  }

  if (max < prefix_len) {
    errx(EXIT_FAILURE, "invalid prefix length %d.", prefix_len);
  }

  q = prefix_len >> 3;
  r = prefix_len & 7;
  mask->sa.sa_family = af;
#if !defined(__linux__)
  mask->sa.sa_len = sa_len;
#endif
  memset((void *)p, 0, max / 8);
  if (q > 0) {
    memset((void *)p, 0xff, q);
  }
  if (r > 0) {
    *((u_char *)p + q) = (0xff00 >> r) & 0xff;
  }

  return (0);
}