#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_usage(FILE *stream) {
    fprintf(stream,
            "Usage: tntctl [options] host command [args...]\n"
            "\n"
            "Options:\n"
            "  -p, --port PORT        SSH port (default: 2222)\n"
            "  -l, --login USER       SSH login name for exec identity\n"
            "  --host-key-checking MODE\n"
            "                         OpenSSH host-key mode: yes, accept-new, no\n"
            "  --known-hosts FILE     OpenSSH known_hosts file\n"
            "  -V, --version          Print version and exit\n"
            "  -h, --help             Print this help and exit\n"
            "\n"
            "Commands mirror the TNT SSH exec interface: health, stats, users,\n"
            "tail, dump, post, help, and exit.\n");
}

static bool is_valid_port(const char *value) {
    char *end = NULL;
    long port;

    if (!value || value[0] == '\0') {
        return false;
    }

    errno = 0;
    port = strtol(value, &end, 10);
    return errno == 0 && end && *end == '\0' && port > 0 && port <= 65535;
}

static bool is_safe_ssh_token(const char *value) {
    const unsigned char *p = (const unsigned char *)value;

    if (!value || value[0] == '\0' || value[0] == '-') {
        return true;
    }
    while (*p) {
        if (isspace(*p) || iscntrl(*p) || *p == ';' || *p == '&' ||
            *p == '|' || *p == '`' || *p == '$' || *p == '<' ||
            *p == '>' || *p == '\\') {
            return true;
        }
        p++;
    }
    return false;
}

static bool has_newline(const char *value) {
    const char *p = value;

    while (p && *p) {
        if (*p == '\n' || *p == '\r') {
            return true;
        }
        p++;
    }
    return false;
}

static bool is_host_key_checking_mode(const char *value) {
    return value &&
           (strcmp(value, "yes") == 0 ||
            strcmp(value, "accept-new") == 0 ||
            strcmp(value, "no") == 0);
}

static bool is_known_exec_command(const char *command) {
    return command &&
           (strcmp(command, "health") == 0 ||
            strcmp(command, "stats") == 0 ||
            strcmp(command, "users") == 0 ||
            strcmp(command, "tail") == 0 ||
            strcmp(command, "dump") == 0 ||
            strcmp(command, "post") == 0 ||
            strcmp(command, "help") == 0 ||
            strcmp(command, "exit") == 0);
}

static int build_remote_command(char *buffer, size_t buf_size, int argc,
                                char **argv, int first_arg) {
    size_t pos = 0;

    if (first_arg >= argc) {
        return -1;
    }

    buffer[0] = '\0';
    for (int i = first_arg; i < argc; i++) {
        size_t len;

        if (has_newline(argv[i])) {
            return -1;
        }
        len = strlen(argv[i]);
        if (pos + len + (i > first_arg ? 1u : 0u) >= buf_size) {
            return -1;
        }
        if (i > first_arg) {
            buffer[pos++] = ' ';
        }
        memcpy(buffer + pos, argv[i], len);
        pos += len;
        buffer[pos] = '\0';
    }

    return 0;
}

static int run_ssh(char **ssh_argv) {
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        perror("tntctl: fork");
        return TNT_EXIT_ERROR;
    }

    if (pid == 0) {
        execvp("ssh", ssh_argv);
        perror("tntctl: ssh");
        _exit(TNT_EXIT_UNAVAILABLE);
    }

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("tntctl: waitpid");
            return TNT_EXIT_ERROR;
        }
    }

    if (WIFEXITED(status)) {
        int rc = WEXITSTATUS(status);
        return rc == 255 ? TNT_EXIT_UNAVAILABLE : rc;
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return TNT_EXIT_ERROR;
}

int main(int argc, char **argv) {
    const char *port = "2222";
    const char *login = NULL;
    const char *host_key_checking = NULL;
    const char *known_hosts = NULL;
    char host_key_option[64];
    char known_hosts_option[1024];
    int i;
    const char *host;
    char destination[512];
    char remote_command[MAX_EXEC_COMMAND_LEN];
    char **ssh_argv = NULL;
    int ssh_argc = 0;
    int rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return TNT_EXIT_OK;
        } else if (strcmp(argv[i], "-V") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            printf("tntctl %s\n", TNT_VERSION);
            return TNT_EXIT_OK;
        } else if (strcmp(argv[i], "-p") == 0 ||
                   strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc || !is_valid_port(argv[i + 1])) {
                fprintf(stderr, "tntctl: invalid port\n");
                return TNT_EXIT_USAGE;
            }
            port = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 ||
                   strcmp(argv[i], "--login") == 0) {
            if (i + 1 >= argc || is_safe_ssh_token(argv[i + 1]) ||
                strchr(argv[i + 1], '@')) {
                fprintf(stderr, "tntctl: invalid login\n");
                return TNT_EXIT_USAGE;
            }
            login = argv[++i];
        } else if (strcmp(argv[i], "--host-key-checking") == 0) {
            if (i + 1 >= argc || !is_host_key_checking_mode(argv[i + 1])) {
                fprintf(stderr, "tntctl: invalid host-key checking mode\n");
                return TNT_EXIT_USAGE;
            }
            host_key_checking = argv[++i];
        } else if (strcmp(argv[i], "--known-hosts") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                has_newline(argv[i + 1])) {
                fprintf(stderr, "tntctl: invalid known_hosts path\n");
                return TNT_EXIT_USAGE;
            }
            known_hosts = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "tntctl: unknown option: %s\n", argv[i]);
            print_usage(stderr);
            return TNT_EXIT_USAGE;
        } else {
            break;
        }
    }

    if (i >= argc) {
        fprintf(stderr, "tntctl: missing host\n");
        print_usage(stderr);
        return TNT_EXIT_USAGE;
    }

    host = argv[i++];
    if (is_safe_ssh_token(host)) {
        fprintf(stderr, "tntctl: invalid host\n");
        return TNT_EXIT_USAGE;
    }
    if (login && strchr(host, '@')) {
        fprintf(stderr, "tntctl: use either --login or user@host, not both\n");
        return TNT_EXIT_USAGE;
    }

    if (i >= argc || !is_known_exec_command(argv[i])) {
        fprintf(stderr, "tntctl: unknown or missing command\n");
        return TNT_EXIT_USAGE;
    }

    if (build_remote_command(remote_command, sizeof(remote_command), argc,
                             argv, i) < 0) {
        fprintf(stderr, "tntctl: invalid or too-long command\n");
        return TNT_EXIT_USAGE;
    }

    if (login) {
        int n = snprintf(destination, sizeof(destination), "%s@%s", login,
                         host);
        if (n < 0 || n >= (int)sizeof(destination)) {
            fprintf(stderr, "tntctl: destination too long\n");
            return TNT_EXIT_USAGE;
        }
    } else {
        int n = snprintf(destination, sizeof(destination), "%s", host);
        if (n < 0 || n >= (int)sizeof(destination)) {
            fprintf(stderr, "tntctl: destination too long\n");
            return TNT_EXIT_USAGE;
        }
    }
    if (destination[0] == '-') {
        fprintf(stderr, "tntctl: invalid destination\n");
        return TNT_EXIT_USAGE;
    }

    ssh_argv = calloc((size_t)argc * 2u + 8u, sizeof(*ssh_argv));
    if (!ssh_argv) {
        fprintf(stderr, "tntctl: out of memory\n");
        return TNT_EXIT_ERROR;
    }

    ssh_argv[ssh_argc++] = "ssh";
    ssh_argv[ssh_argc++] = "-p";
    ssh_argv[ssh_argc++] = (char *)port;
    if (host_key_checking) {
        int n = snprintf(host_key_option, sizeof(host_key_option),
                         "StrictHostKeyChecking=%s", host_key_checking);
        if (n < 0 || n >= (int)sizeof(host_key_option)) {
            fprintf(stderr, "tntctl: host-key option too long\n");
            free(ssh_argv);
            return TNT_EXIT_USAGE;
        }
        ssh_argv[ssh_argc++] = "-o";
        ssh_argv[ssh_argc++] = host_key_option;
    }
    if (known_hosts) {
        int n = snprintf(known_hosts_option, sizeof(known_hosts_option),
                         "UserKnownHostsFile=%s", known_hosts);
        if (n < 0 || n >= (int)sizeof(known_hosts_option)) {
            fprintf(stderr, "tntctl: known_hosts option too long\n");
            free(ssh_argv);
            return TNT_EXIT_USAGE;
        }
        ssh_argv[ssh_argc++] = "-o";
        ssh_argv[ssh_argc++] = known_hosts_option;
    }
    ssh_argv[ssh_argc++] = destination;
    ssh_argv[ssh_argc++] = remote_command;
    ssh_argv[ssh_argc] = NULL;

    rc = run_ssh(ssh_argv);
    free(ssh_argv);
    return rc;
}
