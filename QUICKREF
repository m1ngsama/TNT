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

STRUCTURE
  src/main.c          entry, signals
  src/ssh_server.c    SSH, threads
  src/chat_room.c     broadcast
  src/message.c       persistence
  src/tui.c           rendering
  src/utf8.c          unicode

LIMITS
  64 clients max
  100 messages in RAM
  1024 bytes/message

FILES
  HACKING           dev guide
  CHANGELOG.md      changes
  messages.log      chat log
  host_key          SSH key
