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
 *   - capture:  Packet capture (Wireshark-like)
 *   - analyze:  Packet analysis
 *   - scan:     Port scanning (Nmap-like)
 *   - diag:     Network diagnostics (ping, dns, etc.)
 *
 * Usage:
 *   bowie-tools capture --port 6881 --output capture.pcap
 *   bowie-tools analyze capture.pcap
 *   bowie-tools scan 127.0.0.1 --port 6881
 *   bowie-tools diag ping 8.8.8.8
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define TOOLS_VERSION           "0.1.0"
#define CAPTURE_MAX_PACKETS     10000
#define CAPTURE_SNAPLEN         65535
#define SCAN_TIMEOUT_MS         1000

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
    printf("  capture   Capture packets\n");
    printf("  analyze   Analyze pcap file\n");
    printf("  scan      Scan ports\n");
    printf("  diag      Network diagnostics\n");
    printf("\n");
    printf("Run '%s <command> --help' for more info.\n", prog);
    printf("\n");
}

/* ============================================================================
 * COMMAND: CAPTURE
 * ============================================================================ */

static void capture_usage(const char *prog)
{
    printf("Usage: %s capture [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --interface IFACE   Interface (default: any)\n");
    printf("  -p, --port PORT         Filter by port\n");
    printf("  -f, --filter BPF        BPF filter\n");
    printf("  -o, --output FILE       Output pcap file\n");
    printf("  -c, --count N           Max packets (default: %d)\n", CAPTURE_MAX_PACKETS);
    printf("  -t, --timeout MS        Timeout (default: 0 = infinite)\n");
    printf("\n");
}

static int cmd_capture(int argc, char **argv)
{
    const char *interface = "any";
    const char *output = NULL;
    const char *bpf = NULL;
    int port = 0;
    int count = CAPTURE_MAX_PACKETS;
    int timeout_ms = 0;
    int opt;

    /* Parse args */
    static struct option long_opts[] = {
        {"interface", required_argument, 0, 'i'},
        {"port",      required_argument, 0, 'p'},
        {"filter",    required_argument, 0, 'f'},
        {"output",    required_argument, 0, 'o'},
        {"count",     required_argument, 0, 'c'},
        {"timeout",   required_argument, 0, 't'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:p:f:o:c:t:h",
                              long_opts, NULL)) != -1) {
        switch (opt) {
        case 'i': interface = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'f': bpf = optarg; break;
        case 'o': output = optarg; break;
        case 'c': count = atoi(optarg); break;
        case 't': timeout_ms = atoi(optarg); break;
        case 'h': capture_usage(argv[0]); return 0;
        default:  capture_usage(argv[0]); return 1;
        }
    }

    print_banner("capture");
    printf("  Interface: %s\n", interface);
    printf("  Port:      %s\n", port > 0 ? "yes" : "all");
    printf("  Filter:    %s\n", bpf ? bpf : "(none)");
    printf("  Output:    %s\n", output ? output : "(stdout)");
    printf("  Count:     %d\n", count);
    printf("\n");
    printf("  NOTE: Packet capture requires root (CAP_NET_RAW).\n");
    printf("  NOTE: Full implementation is in Phase 3+ (net/tools/capture.c).\n");
    printf("\n");

    /*
     * For now, this is a placeholder that shows the CLI works.
     * The actual capture implementation will be in core/src/net/tools/.
     */

    if (geteuid() != 0) {
        printf("  Error: must run as root\n");
        return 1;
    }

    printf("  Starting capture... (Ctrl+C to stop)\n");

    while (g_running) {
        wingo_thread_sleep_ms(100);
    }

    printf("\n  Capture stopped.\n");
    return 0;
}

/* ============================================================================
 * COMMAND: ANALYZE
 * ============================================================================ */

static void analyze_usage(const char *prog)
{
    printf("Usage: %s analyze <file.pcap> [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -f, --filter FILTER   Display filter\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -x, --hex             Hex dump\n");
    printf("\n");
}

static int cmd_analyze(int argc, char **argv)
{
    const char *file = NULL;
    const char *filter = NULL;
    bool verbose = false;
    bool hex = false;
    int opt;

    static struct option long_opts[] = {
        {"filter",  required_argument, 0, 'f'},
        {"verbose", no_argument,       0, 'v'},
        {"hex",     no_argument,       0, 'x'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "f:vxh",
                              long_opts, NULL)) != -1) {
        switch (opt) {
        case 'f': filter = optarg; break;
        case 'v': verbose = true; break;
        case 'x': hex = true; break;
        case 'h': analyze_usage(argv[0]); return 0;
        default:  analyze_usage(argv[0]); return 1;
        }
    }

    /* Get file from remaining args */
    if (optind < argc) {
        file = argv[optind];
    }

    if (file == NULL) {
        printf("  Error: no file specified\n");
        analyze_usage(argv[0]);
        return 1;
    }

    print_banner("analyze");
    printf("  File:    %s\n", file);
    printf("  Filter:  %s\n", filter ? filter : "(none)");
    printf("  Verbose: %s\n", verbose ? "yes" : "no");
    printf("  Hex:     %s\n", hex ? "yes" : "no");
    printf("\n");
    printf("  NOTE: Full implementation is in Phase 3+ (net/tools/analyze.c).\n");
    printf("\n");

    /* Check file exists */
    if (access(file, R_OK) != 0) {
        printf("  Error: cannot read file: %s\n", strerror(errno));
        return 1;
    }

    printf("  Analyzing...\n");
    printf("  (placeholder — full implementation in Phase 3+)\n");

    return 0;
}

/* ============================================================================
 * COMMAND: SCAN
 * ============================================================================ */

static void scan_usage(const char *prog)
{
    printf("Usage: %s scan <host> [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -p, --port PORT       Single port\n");
    printf("  -r, --range START-END Port range\n");
    printf("  -t, --type TYPE       Scan type: tcp, udp, syn (default: tcp)\n");
    printf("  -T, --timeout MS      Timeout (default: %d)\n", SCAN_TIMEOUT_MS);
    printf("\n");
}

static int scan_port_tcp(const char *host, int port, int timeout_ms)
{
    struct sockaddr_in addr;
    struct timeval tv;
    int sock;
    int rc;

    /* Resolve host */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct hostent *he = gethostbyname(host);
        if (he == NULL) {
            return -1;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    /* Set timeout */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Try connect */
    rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    close(sock);

    if (rc == 0) {
        return 1;   /* Open */
    }

    if (errno == ECONNREFUSED) {
        return 0;   /* Closed */
    }

    return -1;      /* Filtered / error */
}

static int cmd_scan(int argc, char **argv)
{
    const char *host = NULL;
    const char *type = "tcp";
    int port = 0;
    int range_start = 0;
    int range_end = 0;
    int timeout_ms = SCAN_TIMEOUT_MS;
    int opt;

    static struct option long_opts[] = {
        {"port",    required_argument, 0, 'p'},
        {"range",   required_argument, 0, 'r'},
        {"type",    required_argument, 0, 't'},
        {"timeout", required_argument, 0, 'T'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "p:r:t:T:h",
                              long_opts, NULL)) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'r': {
            char *dash = strchr(optarg, '-');
            if (dash != NULL) {
                *dash = '\0';
                range_start = atoi(optarg);
                range_end = atoi(dash + 1);
            }
            break;
        }
        case 't': type = optarg; break;
        case 'T': timeout_ms = atoi(optarg); break;
        case 'h': scan_usage(argv[0]); return 0;
        default:  scan_usage(argv[0]); return 1;
        }
    }

    /* Get host from remaining args */
    if (optind < argc) {
        host = argv[optind];
    }

    if (host == NULL) {
        printf("  Error: no host specified\n");
        scan_usage(argv[0]);
        return 1;
    }

    print_banner("scan");
    printf("  Host:    %s\n", host);
    printf("  Type:    %s\n", type);
    printf("  Port:    %d\n", port);
    printf("  Range:   %d-%d\n", range_start, range_end);
    printf("  Timeout: %d ms\n", timeout_ms);
    printf("\n");

    if (port > 0) {
        int rc = scan_port_tcp(host, port, timeout_ms);
        if (rc == 1) {
            printf("  Port %d/tcp: OPEN\n", port);
        } else if (rc == 0) {
            printf("  Port %d/tcp: CLOSED\n", port);
        } else {
            printf("  Port %d/tcp: FILTERED\n", port);
        }
    } else if (range_start > 0 && range_end >= range_start) {
        printf("  Scanning range %d-%d...\n", range_start, range_end);
        for (int p = range_start; p <= range_end && g_running; p++) {
            int rc = scan_port_tcp(host, p, timeout_ms);
            if (rc == 1) {
                printf("  Port %d/tcp: OPEN\n", p);
            }
        }
    } else {
        printf("  Error: specify --port or --range\n");
        return 1;
    }

    return 0;
}

/* ============================================================================
 * COMMAND: DIAG
 * ============================================================================ */

static void diag_usage(const char *prog)
{
    printf("Usage: %s diag <subcommand> [args]\n", prog);
    printf("\n");
    printf("Subcommands:\n");
    printf("  ping <host>       Ping host\n");
    printf("  dns <hostname>    DNS lookup\n");
    printf("  interfaces        List interfaces\n");
    printf("  routes            Show routes\n");
    printf("\n");
}

static int diag_ping(const char *host)
{
    struct addrinfo hints, *res;
    int rc;

    printf("  Pinging %s...\n", host);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;

    rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0) {
        printf("  Error: %s\n", gai_strerror(rc));
        return 1;
    }

    /* Print resolved IP */
    {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        printf("  Resolved: %s\n", ip);
    }

    freeaddrinfo(res);

    printf("  NOTE: ICMP ping requires root. Full implementation in Phase 3+.\n");
    return 0;
}

static int diag_dns(const char *hostname)
{
    struct addrinfo hints, *res, *p;
    char ip[INET6_ADDRSTRLEN];
    int rc;

    printf("  DNS lookup: %s\n", hostname);

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
        } else {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &sin6->sin6_addr;
            family = "IPv6";
        }

        inet_ntop(p->ai_family, addr, ip, sizeof(ip));
        printf("  %s: %s\n", family, ip);
    }

    freeaddrinfo(res);
    return 0;
}

static int diag_interfaces(void)
{
    printf("  Network interfaces:\n");
    printf("  (use 'ip addr' or 'ifconfig' for full info)\n");
    printf("\n");

    /* Read /proc/net/dev */
    FILE *f = fopen("/proc/net/dev", "r");
    if (f == NULL) {
        printf("  Error: cannot read /proc/net/dev\n");
        return 1;
    }

    char line[256];
    /* Skip headers */
    fgets(line, sizeof(line), f);
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f) != NULL) {
        char name[64];
        unsigned long rx_bytes, rx_packets, tx_bytes, tx_packets;

        if (sscanf(line, " %63[^:]: %lu %lu %*u %*u %*u %*u %*u %*u %lu %lu",
                   name, &rx_bytes, &rx_packets, &tx_bytes, &tx_packets) >= 5) {
            printf("  %-15s RX: %10lu bytes  TX: %10lu bytes\n",
                   name, rx_bytes, tx_bytes);
        }
    }

    fclose(f);
    return 0;
}

static int diag_routes(void)
{
    printf("  Routing table:\n");

    FILE *f = fopen("/proc/net/route", "r");
    if (f == NULL) {
        printf("  Error: cannot read /proc/net/route\n");
        return 1;
    }

    char line[256];
    /* Skip header */
    fgets(line, sizeof(line), f);

    printf("  %-15s %-15s %-15s %s\n", "Iface", "Destination", "Gateway", "Flags");
    printf("  %-15s %-15s %-15s %s\n", "-----", "-----------", "-------", "-----");

    while (fgets(line, sizeof(line), f) != NULL) {
        char iface[64];
        unsigned long dest, gw, flags;

        if (sscanf(line, "%63s %lx %lx %lx", &iface, &dest, &gw, &flags) >= 4) {
            struct in_addr dest_addr, gw_addr;
            char dest_str[16], gw_str[16];

            dest_addr.s_addr = dest;
            gw_addr.s_addr = gw;

            inet_ntop(AF_INET, &dest_addr, dest_str, sizeof(dest_str));
            inet_ntop(AF_INET, &gw_addr, gw_str, sizeof(gw_str));

            printf("  %-15s %-15s %-15s 0x%lx\n",
                   iface, dest_str, gw_str, flags);
        }
    }

    fclose(f);
    return 0;
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

    if (strcmp(subcmd, "ping") == 0) {
        if (argc < 2) {
            printf("  Error: ping requires host\n");
            return 1;
        }
        return diag_ping(argv[1]);
    } else if (strcmp(subcmd, "dns") == 0) {
        if (argc < 2) {
            printf("  Error: dns requires hostname\n");
            return 1;
        }
        return diag_dns(argv[1]);
    } else if (strcmp(subcmd, "interfaces") == 0) {
        return diag_interfaces();
    } else if (strcmp(subcmd, "routes") == 0) {
        return diag_routes();
    } else {
        printf("  Error: unknown subcommand '%s'\n", subcmd);
        diag_usage(argv[0]);
        return 1;
    }
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
    if (strcmp(cmd, "capture") == 0) {
        rc = cmd_capture(argc - 1, argv + 1);
    } else if (strcmp(cmd, "analyze") == 0) {
        rc = cmd_analyze(argc - 1, argv + 1);
    } else if (strcmp(cmd, "scan") == 0) {
        rc = cmd_scan(argc - 1, argv + 1);
    } else if (strcmp(cmd, "diag") == 0) {
        rc = cmd_diag(argc - 1, argv + 1);
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
