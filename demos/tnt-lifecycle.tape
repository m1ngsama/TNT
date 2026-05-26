# TNT lifecycle demo.
#
# Run from the repository root after building:
#
#   make
#   vhs demos/tnt-lifecycle.tape
#
# The generated GIF is intentionally ignored by git; commit the tape, not the
# rendered artifact.

Output demos/tnt-lifecycle.gif

Require ssh

Set Shell "bash"
Set FontSize 28
Set Width 1200
Set Height 720
Set Theme "Catppuccin Mocha"
Set TypingSpeed 35ms
Set Padding 16
Set WindowBar Colorful

Hide
Type "STATE_DIR=$(mktemp -d /tmp/tnt-vhs.XXXXXX); PORT=22333; TNT_LANG=en ./tnt --bind 127.0.0.1 --public-host demo.local --rate-limit 0 --idle-timeout 0 -p $PORT -d $STATE_DIR >/tmp/tnt-vhs.log 2>&1 & TNT_PID=$!; sleep 1; clear" Enter
Show

Type "ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p $PORT demo@127.0.0.1" Enter
Sleep 1s
Type "demo" Enter
Sleep 1s
Type "hello from TNT" Enter
Sleep 800ms
Escape
Sleep 500ms
Type ":help" Enter
Sleep 2s
Type "q"
Sleep 600ms
Type ":last 5" Enter
Sleep 2s
Type "q"
Sleep 600ms
Type ":search TNT" Enter
Sleep 2s
Type "q"
Sleep 600ms
Type "i"
Sleep 300ms
Type "/me ships terminal chat over SSH" Enter
Sleep 2s
Ctrl+C
Sleep 300ms
Ctrl+C
Sleep 1s

Hide
Type "kill $TNT_PID >/dev/null 2>&1; rm -rf $STATE_DIR; clear" Enter
Show
