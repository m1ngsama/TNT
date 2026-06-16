#include "ssh_server.h"
#include "bootstrap.h"
#include "commands.h"
#include "config_defaults.h"
#include "exec.h"
#include "input.h"
#include "ratelimit.h"
#include "tui.h"
#include "utf8.h"
#include <libssh/libssh.h>
#include <libssh/libssh_version.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <limits.h>

#define TNT_SESSION_THREAD_STACK_SIZE ((size_t)1024 * 1024)

/* Global SSH bind instance */
static ssh_bind g_sshbind = NULL;
static int g_listen_port = TNT_DEFAULT_PORT;

static time_t g_server_start_time = 0;

time_t ssh_server_start_time(void) {
    return g_server_start_time;
}

/* Configuration from environment variables.  Rate-limiting moved to ratelimit.{c,h},
 * the access token to bootstrap.{c,h}, and the idle timeout to input.{c,h}. */

static int generate_rsa_host_key(ssh_key *key) {
#if defined(LIBSSH_VERSION_INT) && LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 12, 0)
    ssh_pki_ctx pki_ctx = ssh_pki_ctx_new();
    int rsa_bits = 4096;
    int rc;

    if (!pki_ctx) {
        return -1;
    }
    if (ssh_pki_ctx_options_set(pki_ctx, SSH_PKI_OPTION_RSA_KEY_SIZE,
                                &rsa_bits) < 0) {
        ssh_pki_ctx_free(pki_ctx);
        return -1;
    }

    rc = ssh_pki_generate_key(SSH_KEYTYPE_RSA, pki_ctx, key);
    ssh_pki_ctx_free(pki_ctx);
    return rc;
#else
    return ssh_pki_generate(SSH_KEYTYPE_RSA, 4096, key);
#endif
}

/* Generate or load SSH host key */
static int setup_host_key(ssh_bind sshbind) {
    struct stat st;
    char host_key_path[PATH_MAX];

    if (tnt_state_path(host_key_path, sizeof(host_key_path), HOST_KEY_FILE) < 0) {
        fprintf(stderr, "State directory path is too long\n");
        return -1;
    }

    /* Check if host key exists */
    if (stat(host_key_path, &st) == 0) {
        /* Validate file size */
        if (st.st_size == 0) {
            fprintf(stderr, "Warning: Empty key file, regenerating...\n");
            unlink(host_key_path);
            /* Fall through to generate new key */
        } else if (st.st_size > 10 * 1024 * 1024) {
            /* Sanity check: key file shouldn't be > 10MB */
            fprintf(stderr, "Error: Key file too large (%lld bytes)\n", (long long)st.st_size);
            return -1;
        } else {
            /* Verify and fix permissions */
            if ((st.st_mode & 0077) != 0) {
                fprintf(stderr, "Warning: Fixing insecure key file permissions\n");
                chmod(host_key_path, 0600);
            }

            /* Load existing key */
            if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, host_key_path) < 0) {
                fprintf(stderr, "Failed to load host key: %s\n", ssh_get_error(sshbind));
                return -1;
            }
            return 0;
        }
    }

    /* Generate new key */
    printf("Generating new RSA 4096-bit host key...\n");
    ssh_key key;
    if (generate_rsa_host_key(&key) < 0) {
        fprintf(stderr, "Failed to generate RSA key\n");
        return -1;
    }

    /* Create temporary file with secure permissions (atomic operation) */
    char temp_key_file[PATH_MAX];
    if (snprintf(temp_key_file, sizeof(temp_key_file), "%s.tmp.%d",
                 host_key_path, getpid()) >= (int)sizeof(temp_key_file)) {
        fprintf(stderr, "Temporary key path is too long\n");
        ssh_key_free(key);
        return -1;
    }

    /* Set umask to ensure restrictive permissions before file creation */
    mode_t old_umask = umask(0077);

    /* Export key to temporary file */
    if (ssh_pki_export_privkey_file(key, NULL, NULL, NULL, temp_key_file) < 0) {
        fprintf(stderr, "Failed to export host key\n");
        ssh_key_free(key);
        umask(old_umask);
        return -1;
    }

    ssh_key_free(key);

    /* Restore original umask */
    umask(old_umask);

    /* Ensure restrictive permissions */
    chmod(temp_key_file, 0600);

    /* Atomically replace the old key file (if any) */
    if (rename(temp_key_file, host_key_path) < 0) {
        fprintf(stderr, "Failed to rename temporary key file\n");
        unlink(temp_key_file);
        return -1;
    }

    /* Load the newly created key */
    if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, host_key_path) < 0) {
        fprintf(stderr, "Failed to load host key: %s\n", ssh_get_error(sshbind));
        return -1;
    }

    return 0;
}

/* Initialize SSH server */
int ssh_server_init(int port) {
    /* Initialize rate-limit / connection-count subsystem */
    ratelimit_init();

    /* Initialize bootstrap (reads TNT_ACCESS_TOKEN) */
    bootstrap_init();

    /* Idle timeout stays here until input.c is extracted in PR2-M5 */
    /* Initialize idle-timeout subsystem */
    input_init();
    g_listen_port = port;
    g_server_start_time = time(NULL);

    g_sshbind = ssh_bind_new();
    if (!g_sshbind) {
        fprintf(stderr, "Failed to create SSH bind\n");
        return -1;
    }

    /* Set up host key */
    if (setup_host_key(g_sshbind) < 0) {
        ssh_bind_free(g_sshbind);
        return -1;
    }

    /* Bind to port */
    ssh_bind_options_set(g_sshbind, SSH_BIND_OPTIONS_BINDPORT, &port);

    /* Configurable bind address (default: 0.0.0.0) */
    const char *bind_addr = getenv("TNT_BIND_ADDR");
    if (!bind_addr) {
        bind_addr = "0.0.0.0";
    }
    ssh_bind_options_set(g_sshbind, SSH_BIND_OPTIONS_BINDADDR, bind_addr);

    /* Configurable SSH log level (default: SSH_LOG_WARNING=1) */
    int verbosity = env_int("TNT_SSH_LOG_LEVEL", SSH_LOG_WARNING, 0, 4);
    ssh_bind_options_set(g_sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY, &verbosity);

    if (ssh_bind_listen(g_sshbind) < 0) {
        fprintf(stderr, "Failed to bind to port %d: %s\n", port, ssh_get_error(g_sshbind));
        ssh_bind_free(g_sshbind);
        return -1;
    }

    return 0;
}

/* Start SSH server (blocking) */
int ssh_server_start(int unused) {
    (void)unused;
    const char *public_host = getenv("TNT_PUBLIC_HOST");
    pthread_attr_t attr;
    if (!public_host || public_host[0] == '\0') {
        public_host = "localhost";
    }

    printf("TNT chat server listening on port %d (SSH)\n", g_listen_port);
    printf("Connect with: ssh -p %d %s\n", g_listen_port, public_host);
    fflush(stdout);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    {
        size_t stack_size = TNT_SESSION_THREAD_STACK_SIZE;
#ifdef PTHREAD_STACK_MIN
        if (stack_size < PTHREAD_STACK_MIN) {
            stack_size = PTHREAD_STACK_MIN;
        }
#endif
        if (pthread_attr_setstacksize(&attr, stack_size) != 0) {
            fprintf(stderr, "Warning: could not set session thread stack size\n");
        }
    }

    while (1) {
        ssh_session session = ssh_new();
        char client_ip[INET6_ADDRSTRLEN];
        accepted_session_t *accepted;
        pthread_t thread;

        if (!session) {
            fprintf(stderr, "Failed to create SSH session\n");
            continue;
        }

        /* Accept connection */
        if (ssh_bind_accept(g_sshbind, session) != SSH_OK) {
            fprintf(stderr, "Error accepting connection: %s\n", ssh_get_error(g_sshbind));
            ssh_free(session);
            continue;
        }

        bootstrap_peer_ip(session, client_ip, sizeof(client_ip));

        /* Check total connection limit */
        if (!ratelimit_check_and_increment_total()) {
            fprintf(stderr, "Max connections reached, rejecting %s\n", client_ip);
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        if (!ratelimit_check_ip(client_ip)) {
            ratelimit_decrement_total();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        accepted = calloc(1, sizeof(*accepted));
        if (!accepted) {
            ratelimit_release_ip(client_ip);
            ratelimit_decrement_total();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        accepted->session = session;
        snprintf(accepted->client_ip, sizeof(accepted->client_ip), "%s",
                 client_ip);

        if (pthread_create(&thread, &attr, bootstrap_run, accepted) != 0) {
            fprintf(stderr, "Thread creation failed: %s\n", strerror(errno));
            free(accepted);
            ratelimit_release_ip(client_ip);
            ratelimit_decrement_total();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }
    }
    /* Unreachable — the while(1) loop only exits via signal/_exit(). */
}
