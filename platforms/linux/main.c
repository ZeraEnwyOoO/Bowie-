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
 * Bowie — Linux Entry Point
 *
 * Usage:
 *   sudo ./bowie [options]
 *
 * Options:
 *   -h, --help              Show help
 *   -v, --version           Show version
 *   -c, --config FILE       Config file
 *   -l, --log-level LEVEL   Log level
 *   -p, --port PORT         Listen port (default: 6881)
 *   -m, --mode MODE         Mode: host, client, both
 *   -P, --peer HOST:PORT    Peer to connect to
 *       --tun               Enable TUN interface
 *       --tun-name NAME     TUN name (default: bowie0)
 *       --tun-mtu MTU       TUN MTU (default: 1400)
 *       --tun-ip IP         TUN IP (default: 10.0.0.2)
 *       --daemon            Run as daemon
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/core/engine.h"
#include "wingo/platform/platform.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"

#include "platforms/linux/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define BOWIE_DEFAULT_PORT      6881
#define BOWIE_DEFAULT_TUN_NAME  "bowie0"
#define BOWIE_DEFAULT_TUN_MTU   1400
#define BOWIE_DEFAULT_TUN_IP    "10.0.0.2"
#define BOWIE_DEFAULT_TUN_MASK  "255.255.255.0"
#define BOWIE_PID_FILE          "/tmp/bowie.pid"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static wingo_engine_t *g_engine = NULL;
static wingo_tun_t *g_tun = NULL;
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_reload = 0;

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

typedef struct {
    const char *config_file;
    const char *log_level;
    int         port;
    const char *mode;
    const char *peer;
    bool        no_dht;
    bool        no_nat;
    bool        no_crypto;
    bool        tun_enabled;
    const char *tun_name;
    int         tun_mtu;
    const char *tun_ip;
    const char *tun_mask;
    bool        daemon;
    bool        show_help;
    bool        show_version;
} bowie_args_t;

static void args_init(bowie_args_t *args)
{
    memset(args, 0, sizeof(*args));
    args->port = BOWIE_DEFAULT_PORT;
    args->mode = "host";
    args->tun_name = BOWIE_DEFAULT_TUN_NAME;
    args->tun_mtu = BOWIE_DEFAULT_TUN_MTU;
    args->tun_ip = BOWIE_DEFAULT_TUN_IP;
    args->tun_mask = BOWIE_DEFAULT_TUN_MASK;
}

/* ============================================================================
 * SIGNAL HANDLING
 * ============================================================================ */

static void signal_handler(int signum)
{
    switch (signum) {
    case SIGINT:
    case SIGTERM:
        g_running = 0;
        if (g_engine != NULL) {
            wingo_engine_stop(g_engine);
        }
        break;

    case SIGHUP:
        g_reload = 1;
        break;

    default:
        break;
    }
}

static wingo_error_t install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return WINGO_ERR_GENERIC;
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return WINGO_ERR_GENERIC;
    }

    if (sigaction(SIGHUP, &sa, NULL) != 0) {
        return WINGO_ERR_GENERIC;
    }

    signal(SIGPIPE, SIG_IGN);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * ARGUMENT PARSING
 * ============================================================================ */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Bowie — P2P Internet Sharing Tool v%s\n", WINGO_VERSION_STRING);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
    printf("  -c, --config FILE       Config file\n");
    printf("  -l, --log-level LEVEL   Log level (trace, debug, info, warn, error)\n");
    printf("  -p, --port PORT         Listen port (default: %d)\n", BOWIE_DEFAULT_PORT);
    printf("  -m, --mode MODE         Mode: host, client, both\n");
    printf("  -P, --peer HOST:PORT    Peer to connect to (client mode)\n");
    printf("      --no-dht            Disable DHT\n");
    printf("      --no-nat            Disable NAT traversal\n");
    printf("      --no-crypto         Disable encryption\n");
    printf("      --tun               Enable TUN interface\n");
    printf("      --tun-name NAME     TUN name (default: %s)\n", BOWIE_DEFAULT_TUN_NAME);
    printf("      --tun-mtu MTU       TUN MTU (default: %d)\n", BOWIE_DEFAULT_TUN_MTU);
    printf("      --tun-ip IP         TUN IP (default: %s)\n", BOWIE_DEFAULT_TUN_IP);
    printf("      --daemon            Run as daemon\n");
    printf("\n");
}

static void print_version(void)
{
    printf("%s v%s\n", WINGO_NAME, WINGO_VERSION_STRING);
    printf("Platform: %s\n", wingo_platform_name());
    printf("Arch:     %s\n", wingo_platform_arch());
    printf("\n");
    printf("%s\n", WINGO_DESCRIPTION);
}

static wingo_log_level_t parse_log_level(const char *str)
{
    if (str == NULL) return WINGO_LOG_INFO;
    if (strcasecmp(str, "trace") == 0) return WINGO_LOG_TRACE;
    if (strcasecmp(str, "debug") == 0) return WINGO_LOG_DEBUG;
    if (strcasecmp(str, "info") == 0) return WINGO_LOG_INFO;
    if (strcasecmp(str, "notice") == 0) return WINGO_LOG_NOTICE;
    if (strcasecmp(str, "warn") == 0) return WINGO_LOG_WARN;
    if (strcasecmp(str, "error") == 0) return WINGO_LOG_ERROR;
    if (strcasecmp(str, "fatal") == 0) return WINGO_LOG_FATAL;
    if (strcasecmp(str, "none") == 0) return WINGO_LOG_NONE;
    return WINGO_LOG_INFO;
}

static int parse_args(int argc, char **argv, bowie_args_t *args)
{
    static struct option long_options[] = {
        {"help",        no_argument,       0, 'h'},
        {"version",     no_argument,       0, 'v'},
        {"config",      required_argument, 0, 'c'},
        {"log-level",   required_argument, 0, 'l'},
        {"port",        required_argument, 0, 'p'},
        {"mode",        required_argument, 0, 'm'},
        {"peer",        required_argument, 0, 'P'},
        {"no-dht",      no_argument,       0, 1000},
        {"no-nat",      no_argument,       0, 1001},
        {"no-crypto",   no_argument,       0, 1002},
        {"tun",         no_argument,       0, 1003},
        {"tun-name",    required_argument, 0, 1004},
        {"tun-mtu",     required_argument, 0, 1005},
        {"tun-ip",      required_argument, 0, 1006},
        {"daemon",      no_argument,       0, 1007},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hvc:l:p:m:P:",
                              long_options, &option_index)) != -1) {
        switch (opt) {
        case 'h': args->show_help = true; return 0;
        case 'v': args->show_version = true; return 0;
        case 'c': args->config_file = optarg; break;
        case 'l': args->log_level = optarg; break;
        case 'p': args->port = atoi(optarg); break;
        case 'm': args->mode = optarg; break;
        case 'P': args->peer = optarg; break;

        case 1000: args->no_dht = true; break;
        case 1001: args->no_nat = true; break;
        case 1002: args->no_crypto = true; break;
        case 1003: args->tun_enabled = true; break;
        case 1004: args->tun_name = optarg; break;
        case 1005: args->tun_mtu = atoi(optarg); break;
        case 1006: args->tun_ip = optarg; break;
        case 1007: args->daemon = true; break;

        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

/* ============================================================================
 * TUN SETUP
 * ============================================================================ */

static wingo_tun_t *setup_tun(const bowie_args_t *args)
{
    wingo_tun_config_t config;

    if (!args->tun_enabled) {
        return NULL;
    }

    /* Check root */
    if (!wingo_platform_is_root()) {
        fprintf(stderr, "Error: TUN requires root privileges\n");
        return NULL;
    }

    /* Check TUN available */
    if (!wingo_platform_has_tun()) {
        fprintf(stderr, "Error: /dev/net/tun not available\n");
        return NULL;
    }

    /* Configure TUN */
    wingo_tun_config_default(&config);
    config.name = args->tun_name;
    config.mtu = args->tun_mtu;
    config.ipv4_addr = args->tun_ip;
    config.ipv4_netmask = args->tun_mask;

    /* Open TUN */
    wingo_tun_t *tun = wingo_tun_open(&config);
    if (tun == NULL) {
        fprintf(stderr, "Error: failed to open TUN\n");
        return NULL;
    }

    return tun;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(int argc, char **argv)
{
    bowie_args_t args;
    wingo_engine_config_t config;
    wingo_error_t rc;

    /* Parse arguments */
    args_init(&args);
    if (parse_args(argc, argv, &args) != 0) {
        return EXIT_FAILURE;
    }

    if (args.show_help) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (args.show_version) {
        print_version();
        return EXIT_SUCCESS;
    }

    /* Daemonize if requested */
    if (args.daemon) {
        rc = wingo_linux_daemonize();
        if (rc != WINGO_SUCCESS) {
            fprintf(stderr, "Error: failed to daemonize\n");
            return EXIT_FAILURE;
        }
    }

    /* Write PID file */
    wingo_linux_write_pidfile(BOWIE_PID_FILE);

    /* Initialize platform */
    rc = wingo_platform_init();
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: failed to init platform\n");
        wingo_linux_remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Setup TUN (if requested) */
    g_tun = setup_tun(&args);
    if (args.tun_enabled && g_tun == NULL) {
        fprintf(stderr, "Error: TUN setup failed\n");
        wingo_platform_shutdown();
        wingo_linux_remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Initialize engine config */
    wingo_engine_config_default(&config);
    config.name = "bowie";
    config.log_level = parse_log_level(args.log_level);
    config.log_color = !args.daemon;
    config.enable_dht = !args.no_dht;
    config.enable_nat = !args.no_nat;
    config.enable_crypto = !args.no_crypto;
    config.enable_tunnel = args.tun_enabled;

    /* Create engine */
    g_engine = wingo_engine_new(&config);
    if (g_engine == NULL) {
        fprintf(stderr, "Error: failed to create engine\n");
        if (g_tun != NULL) wingo_tun_close(g_tun);
        wingo_platform_shutdown();
        wingo_linux_remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Install signal handlers */
    rc = install_signal_handlers();
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: failed to install signal handlers\n");
        wingo_engine_free(g_engine);
        if (g_tun != NULL) wingo_tun_close(g_tun);
        wingo_platform_shutdown();
        wingo_linux_remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Initialize engine */
    rc = wingo_engine_init(g_engine);
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: failed to init engine: %s\n",
                wingo_error_str(rc));
        wingo_engine_free(g_engine);
        if (g_tun != NULL) wingo_tun_close(g_tun);
        wingo_platform_shutdown();
        wingo_linux_remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Print banner (if foreground) */
    if (!args.daemon) {
        printf("\n");
        printf("  ╔═══════════════════════════════════════╗\n");
        printf("  ║                                       ║\n");
        printf("  ║   Bowie — P2P Internet Sharing        ║\n");
        printf("  ║   Version %-28s ║\n", WINGO_VERSION_STRING);
        printf("  ║                                       ║\n");
        printf("  ╚═══════════════════════════════════════╝\n");
        printf("\n");
        printf("  Platform:    %s (%s)\n", wingo_platform_name(), wingo_platform_arch());
        printf("  Mode:        %s\n", args.mode);
        printf("  Port:        %d\n", args.port);
        printf("  TUN:         %s\n", args.tun_enabled ? "enabled" : "disabled");
        if (args.tun_enabled && g_tun != NULL) {
            printf("  TUN name:    %s\n", wingo_tun_get_name(g_tun));
            printf("  TUN MTU:     %d\n", wingo_tun_get_mtu(g_tun));
            printf("  TUN IP:      %s\n", args.tun_ip);
        }
        printf("  DHT:         %s\n", args.no_dht ? "disabled" : "enabled");
        printf("  NAT:         %s\n", args.no_nat ? "disabled" : "enabled");
        printf("  Crypto:      %s\n", args.no_crypto ? "disabled" : "enabled");
        printf("  PID file:    %s\n", BOWIE_PID_FILE);
        printf("\n");
        printf("  Press Ctrl+C to stop.\n");
        printf("\n");
    }

    /* Run engine */
    rc = wingo_engine_run(g_engine);
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: engine failed: %s\n", wingo_error_str(rc));
    }

    /* Shutdown */
    if (!args.daemon) {
        printf("\nShutting down...\n");
    }

    /* Cleanup */
    wingo_engine_free(g_engine);
    g_engine = NULL;

    if (g_tun != NULL) {
        wingo_tun_close(g_tun);
        g_tun = NULL;
    }

    wingo_platform_shutdown();
    wingo_linux_remove_pidfile(BOWIE_PID_FILE);

    if (!args.daemon) {
        printf("Bowie stopped.\n");
    }

    return (rc == WINGO_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
