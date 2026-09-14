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
 * This is the main entry point for the Linux platform.
 *
 * Usage:
 *   sudo ./bowie [options]
 *
 * Options:
 *   -h, --help              Show help
 *   -v, --version           Show version
 *   -c, --config FILE       Config file
 *   -l, --log-level LEVEL   Log level (trace, debug, info, warn, error)
 *   -p, --port PORT         Listen port (default: 6881)
 *   -m, --mode MODE         Mode: host, client, both (default: host)
 *       --peer HOST:PORT    Peer to connect to (client mode)
 *       --no-dht            Disable DHT
 *       --no-nat            Disable NAT traversal
 *       --no-crypto         Disable encryption
 *       --tun               Enable TUN interface
 *       --tun-name NAME     TUN interface name
 *       --tun-mtu MTU       TUN MTU (default: 1400)
 *       --daemon            Run as daemon
 *       --foreground        Run in foreground (default)
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/core/engine.h"
#include "wingo/platform/platform.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"

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
#define BOWIE_PID_FILE          "/tmp/bowie.pid"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static wingo_engine_t *g_engine = NULL;
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

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DAEMON
 * ============================================================================ */

static wingo_error_t daemonize(void)
{
    pid_t pid;
    int fd;

    /* Fork #1 */
    pid = fork();
    if (pid < 0) {
        return WINGO_ERR_GENERIC;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Become session leader */
    if (setsid() < 0) {
        return WINGO_ERR_GENERIC;
    }

    /* Fork #2 */
    pid = fork();
    if (pid < 0) {
        return WINGO_ERR_GENERIC;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change working directory */
    if (chdir("/") < 0) {
        return WINGO_ERR_GENERIC;
    }

    /* Redirect stdio to /dev/null */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }

    /* Set umask */
    umask(027);

    return WINGO_SUCCESS;
}

static wingo_error_t write_pidfile(const char *path)
{
    FILE *f;
    pid_t pid = getpid();

    f = fopen(path, "w");
    if (f == NULL) {
        return WINGO_ERR_FILE_OPEN;
    }

    fprintf(f, "%d\n", pid);
    fclose(f);

    return WINGO_SUCCESS;
}

static void remove_pidfile(const char *path)
{
    unlink(path);
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
    printf("  -m, --mode MODE         Mode: host, client, both (default: host)\n");
    printf("      --peer HOST:PORT    Peer to connect to (client mode)\n");
    printf("      --no-dht            Disable DHT\n");
    printf("      --no-nat            Disable NAT traversal\n");
    printf("      --no-crypto         Disable encryption\n");
    printf("      --tun               Enable TUN interface\n");
    printf("      --tun-name NAME     TUN interface name (default: %s)\n", BOWIE_DEFAULT_TUN_NAME);
    printf("      --tun-mtu MTU       TUN MTU (default: %d)\n", BOWIE_DEFAULT_TUN_MTU);
    printf("      --daemon            Run as daemon\n");
    printf("      --foreground        Run in foreground (default)\n");
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
    if (str == NULL) {
        return WINGO_LOG_INFO;
    }

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
        {"no-dht",      no_argument,       0, 'D'},
        {"no-nat",      no_argument,       0, 'N'},
        {"no-crypto",   no_argument,       0, 'C'},
        {"tun",         no_argument,       0, 'T'},
        {"tun-name",    required_argument, 0, 'n'},
        {"tun-mtu",     required_argument, 0, 'M'},
        {"daemon",      no_argument,       0, 'd'},
        {"foreground",  no_argument,       0, 'f'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hvc:l:p:m:P:DNC Tn:M:df",
                              long_options, &option_index)) != -1) {
        switch (opt) {
        case 'h':
            args->show_help = true;
            return 0;

        case 'v':
            args->show_version = true;
            return 0;

        case 'c':
            args->config_file = optarg;
            break;

        case 'l':
            args->log_level = optarg;
            break;

        case 'p':
            args->port = atoi(optarg);
            break;

        case 'm':
            args->mode = optarg;
            break;

        case 'P':
            args->peer = optarg;
            break;

        case 'D':
            args->no_dht = true;
            break;

        case 'N':
            args->no_nat = true;
            break;

        case 'C':
            args->no_crypto = true;
            break;

        case 'T':
            args->tun_enabled = true;
            break;

        case 'n':
            args->tun_name = optarg;
            break;

        case 'M':
            args->tun_mtu = atoi(optarg);
            break;

        case 'd':
            args->daemon = true;
            break;

        case 'f':
            args->daemon = false;
            break;

        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
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

    /* Check root (needed for TUN) */
    if (args.tun_enabled && !wingo_platform_is_root()) {
        fprintf(stderr, "Error: TUN requires root privileges\n");
        fprintf(stderr, "Try: sudo %s %s\n", argv[0], "--tun");
        return EXIT_FAILURE;
    }

    /* Daemonize if requested */
    if (args.daemon) {
        rc = daemonize();
        if (rc != WINGO_SUCCESS) {
            fprintf(stderr, "Error: failed to daemonize\n");
            return EXIT_FAILURE;
        }
    }

    /* Write PID file */
    write_pidfile(BOWIE_PID_FILE);

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
        remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Install signal handlers */
    rc = install_signal_handlers();
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: failed to install signal handlers\n");
        wingo_engine_free(g_engine);
        remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Initialize engine */
    rc = wingo_engine_init(g_engine);
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: failed to init engine: %s\n",
                wingo_error_str(rc));
        wingo_engine_free(g_engine);
        remove_pidfile(BOWIE_PID_FILE);
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
        printf("  DHT:         %s\n", args.no_dht ? "disabled" : "enabled");
        printf("  NAT:         %s\n", args.no_nat ? "disabled" : "enabled");
        printf("  Crypto:      %s\n", args.no_crypto ? "disabled" : "enabled");
        printf("\n");
        printf("  Press Ctrl+C to stop.\n");
        printf("\n");
    }

    /* Run engine */
    rc = wingo_engine_run(g_engine);
    if (rc != WINGO_SUCCESS) {
        fprintf(stderr, "Error: engine failed: %s\n", wingo_error_str(rc));
        wingo_engine_free(g_engine);
        remove_pidfile(BOWIE_PID_FILE);
        return EXIT_FAILURE;
    }

    /* Shutdown */
    if (!args.daemon) {
        printf("\nShutting down...\n");
    }

    wingo_engine_free(g_engine);
    remove_pidfile(BOWIE_PID_FILE);

    if (!args.daemon) {
        printf("Bowie stopped.\n");
    }

    return EXIT_SUCCESS;
}
