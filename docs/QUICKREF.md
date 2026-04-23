TNT Quick Reference
===================

BUILD
  make              production build
  make debug        debug symbols
  make asan         memory sanitizer
  make release      optimized + stripped
  make clean        remove artifacts

TEST
  ./test_basic.sh           basic functionality
  ./test_stress.sh 20 60    stress test (20 clients, 60s)

DEBUG
  ASAN_OPTIONS=detect_leaks=1 ./tnt
  valgrind --leak-check=full ./tnt
  make check

COMMANDS (COMMAND mode, prefix with :)
  list, users, who       show online users
  nick <name>            change nickname
  msg <user> <text>      whisper to user
  w <user> <text>        alias for msg
  last [N]               last N messages from log (default 10, max 50)
  search <keyword>       search full history (case-insensitive, 15 results)
  mute-joins             toggle join/leave notifications
  help                   show all commands
  clear                  clear output
  q / quit / exit        disconnect

INSERT MODE
  /me <action>           action message
  @username              mention (bell + highlight)

STRUCTURE
  src/main.c          entry, signals
  src/ssh_server.c    SSH, threads, commands
  src/chat_room.c     broadcast
  src/message.c       persistence, search
  src/tui.c           rendering, help
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
