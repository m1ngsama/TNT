TNT Quick Reference
===================

BUILD
  make              production build
  make debug        debug symbols
  make asan         memory sanitizer
  make release      optimized + stripped
  make clean        remove artifacts

TEST
  make test                 strict unit + integration tests
  make test-advisory        unit tests + advisory integration checks
  make anonymous-access-test default anonymous login checks
  make connection-limit-test per-IP concurrency/rate-limit checks
  make security-test        security feature checks
  make stress-test          concurrent-client stress test
  make soak-test            idle/reconnect/control-plane soak test
  make slow-client-test     slow interactive-client backpressure test
  make user-lifecycle-test  two-user TUI lifecycle test
  make ci-test              same checks as GitHub Actions

DEBUG
  ASAN_OPTIONS=detect_leaks=1 ./tnt
  valgrind --leak-check=full ./tnt
  make check

COMMANDS (COMMAND mode, prefix with :)
  list, users, who       show online users
  nick <name>            change nickname
  msg <user> <message>   send private message
  w <user> <text>        alias for msg
  reply <text>           reply to latest private message
  r <text>               alias for reply
  inbox                  show private messages, newest first
  inbox clear            clear private messages for this session
  last [N]               last N messages from log (default 10, max 50)
  search <keyword>       search full history (case-insensitive, 15 results)
  mute-joins             toggle join/leave notifications
  help                   concise manual
  lang [en|zh]           show or switch UI language
  clear                  clear output
  q / quit / exit        disconnect

INSERT MODE
  /me <action>           action message
  @username              mention (bell + highlight)
  paste                  multi-line paste stays in the input buffer
  limit                  1023 bytes/message; over-limit input rings bell
  normal                 opens/follows latest; k/PgUp older, j/PgDn newer
  insert aliases         i/a/o enter INSERT mode from NORMAL

EXEC COMMANDS
  health                 print service health
  stats [--json]         print room statistics
  users [--json]         list online users
  tail [N] / tail -n N   recent in-memory room messages
  dump [N] / dump -n N   persisted messages.log v1 records
  post <message>         post as the SSH login name

MAINTENANCE
  scripts/logrotate.sh LOG_FILE MAX_SIZE_MB KEEP_LINES
                           archive and compact messages.log
  scripts/logrotate.sh --dry-run ...
                           preview log maintenance actions
  tnt --log-check LOG_FILE  audit messages.log v1 records
  tnt --log-recover LOG_FILE > OUT
                           write valid records to stdout

STRUCTURE
  src/main.c          entry, signals
  src/cli_text.c      startup CLI text
  src/tntctl_text.c   tntctl local help and diagnostics
  src/command_catalog.c command metadata, usage, argument shape
  src/ssh_server.c    SSH listener and server setup
  src/bootstrap.c     SSH auth/session bootstrap
  src/chat_room.c     broadcast and room state
  src/commands.c      COMMAND-mode command dispatch
  src/exec_catalog.c  SSH exec command matching, usage, argument shape
  src/exec.c          SSH exec command dispatch
  src/message.c       persistence, search
  src/message_log.c   messages.log v1 parsing and formatting
  src/message_log_tool.c offline messages.log check/recover CLI
  src/history_view.c  message viewport / scroll state
  src/help_text.c     full-screen key reference text
  src/manual.c        concise manual panel rendering
  src/manual_text.c   concise manual content
  src/i18n.c          UI language and locale selection
  src/i18n_text.c     shared UI text catalog
  src/ratelimit.c     connection limits and rate limiting
  src/tui.c           rendering
  src/tui_status.c    status/input line rendering
  src/utf8.c          unicode

LIMITS
  64 clients max (configurable)
  100 messages in RAM; unlimited on disk
  1024 bytes/message

FILES
  messages.log      public chat log (RFC3339; excludes private messages)
  host_key          SSH key (auto-generated)
  motd.txt          message of the day (optional)
  CHANGELOG.md      version history
