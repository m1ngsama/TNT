function __tntctl_no_command
    not __fish_seen_subcommand_from health stats users tail dump post help exit
end

complete -c tntctl -s p -l port -d 'Server port' -x -n __tntctl_no_command
complete -c tntctl -s l -l login -d 'Login user' -x -n __tntctl_no_command
complete -c tntctl -l host-key-checking -d 'SSH host key checking' -xa 'yes accept-new no' -n __tntctl_no_command
complete -c tntctl -l known-hosts -d 'known_hosts file' -r -n __tntctl_no_command
complete -c tntctl -s h -l help -d 'Show help' -n __tntctl_no_command
complete -c tntctl -s V -l version -d 'Show version' -n __tntctl_no_command
complete -c tntctl -l json -d 'JSON output' -n '__fish_seen_subcommand_from users stats'
complete -c tntctl -s n -d 'Record count' -x -n '__fish_seen_subcommand_from tail dump'
complete -c tntctl -l all -d 'All records' -n '__fish_seen_subcommand_from dump'

complete -c tntctl -f -a health -d 'Print service health' -n __tntctl_no_command
complete -c tntctl -f -a stats  -d 'Print room statistics' -n __tntctl_no_command
complete -c tntctl -f -a users  -d 'List online users' -n __tntctl_no_command
complete -c tntctl -f -a tail   -d 'Print recent messages' -n __tntctl_no_command
complete -c tntctl -f -a dump   -d 'Export persisted messages' -n __tntctl_no_command
complete -c tntctl -f -a post   -d 'Post a message' -n __tntctl_no_command
complete -c tntctl -f -a help   -d 'Show exec help' -n __tntctl_no_command
complete -c tntctl -f -a exit   -d 'Exit successfully' -n __tntctl_no_command
