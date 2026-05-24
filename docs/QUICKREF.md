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
  make ci-test              same checks as GitHub Actions

DEBUG
  ASAN_OPTIONS=detect_leaks=1 ./tnt
  valgrind --leak-check=full ./tnt
  make check

COMMANDS (COMMAND mode, prefix with :)
  list, users, who       show online users
  nick <name>            change nickname
  msg <user> <text>      whisper to user
  w <user> <text>        alias for msg
  inbox                  show whispers
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

STRUCTURE
  src/main.c          entry, signals
  src/cli_text.c      startup CLI text
  src/ssh_server.c    SSH listener and server setup
  src/bootstrap.c     SSH auth/session bootstrap
  src/chat_room.c     broadcast and room state
  src/commands.c      COMMAND-mode command dispatch
  src/exec.c          SSH exec command dispatch
  src/message.c       persistence, search
  src/history_view.c  message viewport / scroll state
  src/help_text.c     full-screen key reference text
  src/manual.c        concise manual panel rendering
  src/manual_text.c   concise manual content
  src/i18n.c          language selection and shared text
  src/ratelimit.c     connection limits and rate limiting
  src/tui.c           rendering
  src/tui_status.c    status/input line rendering
  src/utf8.c          unicode

LIMITS
  64 clients max (configurable)
  100 messages in RAM; unlimited on disk
  1024 bytes/message

FILES
  messages.log      chat log (RFC3339)
  host_key          SSH key (auto-generated)
  motd.txt          message of the day (optional)
  CHANGELOG.md      version history
