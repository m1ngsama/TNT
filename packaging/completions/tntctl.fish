# fish completion for tntctl
#
# Install: copy this file to ~/.config/fish/completions/tntctl.fish

# Options
complete -c tntctl -s p -l port -d 'Server port' -x
complete -c tntctl -s l -l login -d 'Login user' -x
complete -c tntctl -l host-key-checking -d 'SSH host key checking' -xa 'yes accept-new no'
complete -c tntctl -l known-hosts -d 'known_hosts file' -r
complete -c tntctl -s h -l help -d 'Show help'
complete -c tntctl -s V -l version -d 'Show version'
complete -c tntctl -l json -d 'JSON output' -n '__fish_seen_subcommand_from users stats'

# Subcommands (typed after the host)
complete -c tntctl -f -a health -d 'Print service health'
complete -c tntctl -f -a stats  -d 'Print room statistics'
complete -c tntctl -f -a users  -d 'List online users'
complete -c tntctl -f -a tail   -d 'Print recent messages'
complete -c tntctl -f -a dump   -d 'Export persisted messages'
complete -c tntctl -f -a post   -d 'Post a message'
complete -c tntctl -f -a help   -d 'Show exec help'
complete -c tntctl -f -a exit   -d 'Exit successfully'
