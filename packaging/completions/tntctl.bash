# bash completion for tntctl
#
# Install: source this file from ~/.bashrc, or copy it to
#   /usr/share/bash-completion/completions/tntctl
# (or /etc/bash_completion.d/tntctl).

_tntctl() {
    local cur prev opts commands cmd i
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    opts="-p --port -l --login --host-key-checking --known-hosts -h --help -V --version"
    commands="health stats users tail dump post help exit"

    case "$prev" in
        --host-key-checking)
            COMPREPLY=( $(compgen -W "yes accept-new no" -- "$cur") )
            return ;;
        --known-hosts)
            COMPREPLY=( $(compgen -f -- "$cur") )
            return ;;
        -p|--port|-l|--login)
            return ;;  # free-form value
    esac

    # Detect a subcommand already on the line.
    cmd=""
    for ((i = 1; i < COMP_CWORD; i++)); do
        case "${COMP_WORDS[i]}" in
            health|stats|users|tail|dump|post|help|exit)
                cmd="${COMP_WORDS[i]}"; break ;;
        esac
    done

    if [[ "$cur" == -* ]]; then
        local extra=""
        case "$cmd" in
            users|stats) extra="--json" ;;
        esac
        COMPREPLY=( $(compgen -W "$opts $extra" -- "$cur") )
        return
    fi

    # Positional args after a subcommand are free-form.
    if [[ -n "$cmd" ]]; then
        return
    fi

    # Otherwise offer the known subcommands (typed after the host).
    COMPREPLY=( $(compgen -W "$commands" -- "$cur") )
}
complete -F _tntctl tntctl
