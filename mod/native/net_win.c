/* net_win.c — tuxmc_net on Windows: this process's TCP connections from the
 * IP Helper tables (what `netstat -ano` reads). Byte counters need admin on
 * Windows, so sent/recv stay 0 and the network map shows the links without
 * rates. UDP has no remote address there, so only TCP is listed. */
#include "tuxmc.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(tuxmc_conn_t) == 40, "Java reads 40-byte records");

static void *table(ULONG family)
{
    DWORD size = 0;
    void *t = NULL;
    for (int tries = 0; tries < 4; tries++) {
        DWORD r = GetExtendedTcpTable(t, &size, FALSE, family, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (r == NO_ERROR)
            return t;
        free(t);
        t = NULL;
        if (r != ERROR_INSUFFICIENT_BUFFER)
            return NULL;
        t = malloc(size + 4096);
        if (!t)
            return NULL;
        size += 4096;
    }
    free(t);
    return NULL;
}

int tuxmc_net(tuxmc_conn_t *out, int max)
{
    DWORD me = GetCurrentProcessId();
    int n = 0;

    MIB_TCPTABLE_OWNER_PID *t4 = table(AF_INET);
    if (t4) {
        for (DWORD i = 0; i < t4->dwNumEntries && n < max; i++) {
            MIB_TCPROW_OWNER_PID *r = &t4->table[i];
            if (r->dwOwningPid != me || r->dwState != MIB_TCP_STATE_ESTAB)
                continue;
            tuxmc_conn_t *c = &out[n++];
            memset(c, 0, sizeof *c);
            c->proto = 6;
            c->family = 4;
            c->port = ntohs((u_short)r->dwRemotePort);
            memcpy(c->addr, &r->dwRemoteAddr, 4);
        }
        free(t4);
    }

    MIB_TCP6TABLE_OWNER_PID *t6 = table(AF_INET6);
    if (t6) {
        for (DWORD i = 0; i < t6->dwNumEntries && n < max; i++) {
            MIB_TCP6ROW_OWNER_PID *r = &t6->table[i];
            if (r->dwOwningPid != me || r->dwState != MIB_TCP_STATE_ESTAB)
                continue;
            tuxmc_conn_t *c = &out[n++];
            memset(c, 0, sizeof *c);
            c->proto = 6;
            c->family = 6;
            c->port = ntohs((u_short)r->dwRemotePort);
            memcpy(c->addr, r->ucRemoteAddr, 16);
        }
        free(t6);
    }
    return t4 || t6 ? n : -1;
}
