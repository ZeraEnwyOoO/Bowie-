 /*
 * Wingo — P2P Internet Sharing Tool (Repo: Bowie)
 * Copyright (C) 2024 ASBM Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Bowie Tools — Network Analysis CLI
 *
 * This tool provides built-in network analysis features:
 *   - diag:     Network diagnostics (Phase 3)
 *   - capture:  Packet capture (Phase 4 — TODO)
 *   - analyze:  Packet analysis (Phase 4 — TODO)
 *   - scan:     Port scanning (Phase 4 — TODO)
 *
 * Usage:
 *   bowie-tools diag interfaces
 *   bowie-tools diag routes
 *   bowie-tools diag dns <hostname>
 *   bowie-tools capture ...   (Phase 4)
 *   bowie-tools analyze ...   (Phase 4)
 *   bowie-tools scan ...      (Phase 4)
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/platform/platform.h"
#include "wingo/util/time.h"

#include "platforms/linux/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define TOOLS_VERSION           "0.1.0"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static volatile sig_atomic_t g_running = 1;

/* ============================================================================
 * SIGNAL HANDLING
 * ============================================================================ */

static void signal_handler(int signum)
{
    WINGO_UNUSED(signum);
    g_running = 0;
}

static void install_signals(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
}

/* ============================================================================
 * UTILITY
 * ============================================================================ */

static void print_banner(const char *cmd)
{
    printf("\n");
    printf("  Bowie Tools v%s — %s\n", TOOLS_VERSION, cmd);
    printf("\n");
}

static void print_usage(const char *prog)
{
    printf("Usage: %s <command> [options]\n", prog);
    printf("\n");
    printf("Commands:\n");
    printf("  diag      Network diagnostics\n");
    printf("  capture   Capture packets (Phase 4)\n");
    printf("  analyze   Analyze pcap file (Phase 4)\n");
    printf("  scan      Scan ports (Phase 4)\n");
    printf("\n");
    printf("Run '%s <command> --help' for more info.\n", prog);
    printf("\n");
}

/* ============================================================================
 * DIAG: INTERFACES
 * ============================================================================ */

static int diag_interfaces(void)
{
    wingo_linux_if_info_t ifaces[32];
    int count;
    int i;

    printf("  Listing network interfaces...\n");
    printf("\n");

    count = wingo_linux_list_interfaces(ifaces, 32);
    if (count < 0) {
        printf("  Error: failed to list interfaces\n");
        return 1;
    }

    if (count == 0) {
        printf("  No interfaces found\n");
        return 0;
    }

    printf("  %-15s %-18s %-18s %-8s %s\n",
           "Interface", "IPv4", "MAC", "MTU", "State");
    printf("  %-15s %-18s %-18s %-8s %s\n",
           "---------", "----", "---", "---", "-----");

    for (i = 0; i < count; i++) {
        printf("  %-15s %-18s %-18s %-8d %s%s\n",
               ifaces[i].name,
               ifaces[i].ipv4[0] ? ifaces[i].ipv4 : "-",
               ifaces[i].mac[0] ? ifaces[i].mac : "-",
               ifaces[i].mtu > 0 ? ifaces[i].mtu : 0,
               ifaces[i].is_up ? "UP" : "DOWN",
               ifaces[i].is_loopback ? " (loopback)" : "");
    }

    printf("\n");
    printf("  Total: %d interfaces\n", count);
    printf("\n");

    return 0;
}

/* ============================================================================
 * DIAG: ROUTES
 * ============================================================================ */

static int diag_routes(void)
{
    FILE *f;
    char line[256];
    char default_if[64] = {0};

    printf("  Routing table:\n");
    printf("\n");

    f = fopen("/proc/net/route", "r");
    if (f == NULL) {
        printf("  Error: cannot read /proc/net/route\n");
        return 1;
    }

    /* Skip header */
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return 1;
    }

    printf("  %-15s %-15s %-15s %-8s %s\n",
           "Iface", "Destination", "Gateway", "Flags", "Metric");
    printf("  %-15s %-15s %-15s %-8s %s\n",
           "-----", "-----------", "-------", "-----", "------");

    while (fgets(line, sizeof(line), f) != NULL) {
        char iface[64];
        unsigned long dest, gw, flags;
        int refcnt, use, metric, mask, mtu, window, irtt;

        if (sscanf(line, "%63s %lx %lx %lx %d %d %d %x %d %d %d",
                   iface, &dest, &gw, &flags, &refcnt, &use,
                   &metric, &mask, &mtu, &window, &irtt) >= 4) {
            struct in_addr dest_addr, gw_addr;
            char dest_str[16], gw_str[16];

            dest_addr.s_addr = dest;
            gw_addr.s_addr = gw;

            inet_ntop(AF_INET, &dest_addr, dest_str, sizeof(dest_str));
            inet_ntop(AF_INET, &gw_addr, gw_str, sizeof(gw_str));

            printf("  %-15s %-15s %-15s 0x%-6lx %d\n",
                   iface, dest_str, gw_str, flags, metric);

            /* Remember default interface */
            if (dest == 0 && default_if[0] == '\0') {
                strncpy(default_if, iface, sizeof(default_if) - 1);
            }
        }
    }

    fclose(f);

    if (default_if[0] != '\0') {
        printf("\n");
        printf("  Default interface: %s\n", default_if);
    }

    printf("\n");

    return 0;
}

/* ============================================================================
 * DIAG: DNS
 * ============================================================================ */

static int diag_dns(const char *hostname)
{
    struct addrinfo hints, *res, *p;
    char ip[INET6_ADDRSTRLEN];
    int rc;

    printf("  DNS lookup: %s\n", hostname);
    printf("\n");

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(hostname, NULL, &hints, &res);
    if (rc != 0) {
        printf("  Error: %s\n", gai_strerror(rc));
        return 1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        void *addr;
        const char *family;

        if (p->ai_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)p->ai_addr;
            addr = &sin->sin_addr;
            family = "IPv4";
        } else if (p->ai_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &sin6->sin6_addr;
            family = "IPv6";
        } else {
            continue;
        }

        inet_ntop(p->ai_family, addr, ip, sizeof(ip));
        printf("  %s: %s\n", family, ip);
    }

    freeaddrinfo(res);

    printf("\n");
    return 0;
}

/* ============================================================================
 * DIAG: HOSTNAME
 * ============================================================================ */

static int diag_hostname(void)
{
    char hostname[256];

    if (wingo_linux_get_hostname(hostname, sizeof(hostname)) != WINGO_SUCCESS) {
        printf("  Error: failed to get hostname\n");
        return 1;
    }

    printf("  Hostname: %s\n", hostname);
    printf("\n");

    return 0;
}

/* ============================================================================
 * DIAG: UPTIME
 * ============================================================================ */

static int diag_uptime(void)
{
    wingo_i64 uptime = wingo_linux_get_uptime();
    double load[3];

    if (uptime < 0) {
        printf("  Error: failed to get uptime\n");
        return 1;
    }

    printf("  System uptime: %lld seconds\n", (long long)uptime);

    if (wingo_linux_get_loadavg(load) == WINGO_SUCCESS) {
        printf("  Load average:  %.2f, %.2f, %.2f\n",
               load[0], load[1], load[2]);
    }

    printf("\n");
    return 0;
}

/* ============================================================================
 * DIAG COMMAND
 * ============================================================================ */

static void diag_usage(const char *prog)
{
    printf("Usage: %s diag <subcommand> [args]\n", prog);
    printf("\n");
    printf("Subcommands:\n");
    printf("  interfaces        List network interfaces\n");
    printf("  routes            Show routing table\n");
    printf("  dns <hostname>    DNS lookup\n");
    printf("  hostname          Show hostname\n");
    printf("  uptime            Show system uptime\n");
    printf("  ping <host>       Ping host (Phase 4)\n");
    printf("  traceroute <host> Traceroute (Phase 4)\n");
    printf("\n");
}

static int cmd_diag(int argc, char **argv)
{
    const char *subcmd;

    if (argc < 1) {
        diag_usage(argv[0]);
        return 1;
    }

    subcmd = argv[0];

    print_banner("diag");

    if (strcmp(subcmd, "interfaces") == 0) {
        return diag_interfaces();
    } else if (strcmp(subcmd, "routes") == 0) {
        return diag_routes();
    } else if (strcmp(subcmd, "dns") == 0) {
        if (argc < 2) {
            printf("  Error: dns requires hostname\n");
            return 1;
        }
        return diag_dns(argv[1]);
    } else if (strcmp(subcmd, "hostname") == 0) {
        return diag_hostname();
    } else if (strcmp(subcmd, "uptime") == 0) {
        return diag_uptime();
    } else if (strcmp(subcmd, "ping") == 0) {
        printf("  Error: 'ping' requires Phase 4 (Network)\n");
        printf("  NOTE: ICMP ping will be implemented in Phase 4.\n");
        return 1;
    } else if (strcmp(subcmd, "traceroute") == 0) {
        printf("  Error: 'traceroute' requires Phase 4 (Network)\n");
        printf("  NOTE: Traceroute will be implemented in Phase 4.\n");
        return 1;
    } else {
        printf("  Error: unknown subcommand '%s'\n", subcmd);
        diag_usage(argv[0]);
        return 1;
    }
}

/* ============================================================================
 * CAPTURE COMMAND (PHASE 4)
 * ============================================================================ */

static int cmd_capture(int argc, char **argv)
{
    WINGO_UNUSED(argc);
    WINGO_UNUSED(argv);

    print_banner("capture");

    printf("  Error: 'capture' requires Phase 4 (Network)\n");
    printf("\n");
    printf("  NOTE: Packet capture will be implemented in Phase 4.\n");
    printf("        It will use AF_PACKET raw sockets.\n");
    printf("\n");

    return 1;
}

/* ============================================================================
 * ANALYZE COMMAND (PHASE 4)
 * ============================================================================ */

static int cmd_analyze(int argc, char **argv)
{
    WINGO_UNUSED(argc);
    WINGO_UNUSED(argv);

    print_banner("analyze");

    printf("  Error: 'analyze' requires Phase 4 (Network)\n");
    printf("\n");
    printf("  NOTE: Packet analysis will be implemented in Phase 4.\n");
    printf("        It will parse Ethernet/IP/TCP/UDP headers.\n");
    printf("\n");

    return 1;
}

/* ============================================================================
 * SCAN COMMAND (PHASE 4)
 * ============================================================================ */

static int cmd_scan(int argc, char **argv)
{
    WINGO_UNUSED(argc);
    WINGO_UNUSED(argv);

    print_banner("scan");

    printf("  Error: 'scan' requires Phase 4 (Network)\n");
    printf("\n");
    printf("  NOTE: Port scanning will be implemented in Phase 4.\n");
    printf("        It will use TCP connect() and UDP sendto().\n");
    printf("\n");

    return 1;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(int argc, char **argv)
{
    const char *cmd;
    int rc;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Initialize logging */
    wingo_log_init(NULL);
    wingo_log_set_level(WINGO_LOG_WARN);

    /* Install signal handlers */
    install_signals();

    cmd = argv[1];

    /* Dispatch */
    if (strcmp(cmd, "diag") == 0) {
        rc = cmd_diag(argc - 2, argv + 2);
    } else if (strcmp(cmd, "capture") == 0) {
        rc = cmd_capture(argc - 2, argv + 2);
    } else if (strcmp(cmd, "analyze") == 0) {
        rc = cmd_analyze(argc - 2, argv + 2);
    } else if (strcmp(cmd, "scan") == 0) {
        rc = cmd_scan(argc - 2, argv + 2);
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        rc = 0;
    } else if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("bowie-tools v%s\n", TOOLS_VERSION);
        rc = 0;
    } else {
        printf("Error: unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        rc = 1;
    }

    wingo_log_shutdown();
    return rc;
}
