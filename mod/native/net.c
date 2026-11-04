/* net.c — this process's sockets via NETLINK_SOCK_DIAG (what `ss` does,
 * without starting a process every second). */
#include "tuxmc.h"

#include <dirent.h>
#include <stddef.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

_Static_assert(sizeof(tuxmc_conn_t) == 40, "Java reads 40-byte records");

static int cmp_ino(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

/* inodes of our own sockets, from /proc/self/fd */
static int own_inodes(unsigned long *ino, int max)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return 0;
    int n = 0;
    char path[300], link[64];
    for (struct dirent *e; (e = readdir(d)) && n < max;) {
        snprintf(path, sizeof path, "/proc/self/fd/%s", e->d_name);
        ssize_t l = readlink(path, link, sizeof link - 1);
        if (l <= 0)
            continue;
        link[l] = 0;
        if (sscanf(link, "socket:[%lu]", &ino[n]) == 1)
            n++;
    }
    closedir(d);
    qsort(ino, n, sizeof *ino, cmp_ino);
    return n;
}

static int dump(int fd, int family, int proto, const unsigned long *ino, int nino, tuxmc_conn_t *out, int n, int max)
{
    struct {
        struct nlmsghdr nh;
        struct inet_diag_req_v2 r;
    } req = {0};
    req.nh.nlmsg_len = sizeof req;
    req.nh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.r.sdiag_family = family;
    req.r.sdiag_protocol = proto;
    req.r.idiag_states = ~(1u << 10);    /* everything but LISTEN */
    if (proto == IPPROTO_TCP)
        req.r.idiag_ext = 1 << (INET_DIAG_INFO - 1);
    if (send(fd, &req, sizeof req, 0) < 0)
        return n;

    static char buf[65536];
    for (;;) {
        ssize_t len = recv(fd, buf, sizeof buf, 0);
        if (len <= 0)
            return n;
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf; NLMSG_OK(h, (size_t)len); h = NLMSG_NEXT(h, len)) {
            if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
                return n;
            struct inet_diag_msg *m = NLMSG_DATA(h);
            unsigned long key = m->idiag_inode;
            if (!bsearch(&key, ino, nino, sizeof *ino, cmp_ino) || !m->id.idiag_dport || n >= max)
                continue;
            tuxmc_conn_t *c = &out[n];
            memset(c, 0, sizeof *c);
            c->proto = proto;
            c->family = family == AF_INET ? 4 : 6;
            c->port = ntohs(m->id.idiag_dport);
            memcpy(c->addr, m->id.idiag_dst, family == AF_INET ? 4 : 16);
            int alen = h->nlmsg_len - NLMSG_LENGTH(sizeof *m);
            for (struct rtattr *a = (struct rtattr *)(m + 1); RTA_OK(a, alen); a = RTA_NEXT(a, alen))
                if (a->rta_type == INET_DIAG_INFO && RTA_PAYLOAD(a) >= (int)(offsetof(struct tcp_info, tcpi_bytes_received) + 8)) {
                    struct tcp_info *ti = RTA_DATA(a);
                    c->sent = ti->tcpi_bytes_acked;
                    c->recv = ti->tcpi_bytes_received;
                }
            n++;
        }
    }
}

int tuxmc_net(tuxmc_conn_t *out, int max)
{
    unsigned long ino[1024];
    int nino = own_inodes(ino, 1024);
    if (!nino)
        return 0;
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (fd < 0)
        return -1;
    int n = 0;
    static const int fam[] = {AF_INET, AF_INET6}, pr[] = {IPPROTO_TCP, IPPROTO_UDP};
    for (int f = 0; f < 2; f++)
        for (int p = 0; p < 2; p++)
            n = dump(fd, fam[f], pr[p], ino, nino, out, n, max);
    close(fd);
    return n;
}
