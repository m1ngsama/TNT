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
            return ;;
    esac

    cmd=""
    for ((i = 1; i < COMP_CWORD; i++)); do
        case "${COMP_WORDS[i]}" in
            health|stats|users|tail|dump|post|help|exit)
                cmd="${COMP_WORDS[i]}"; break ;;
        esac
    done

    if [[ "$cur" == -* ]]; then
        case "$cmd" in
            users|stats) COMPREPLY=( $(compgen -W "--json" -- "$cur") ) ;;
            tail) COMPREPLY=( $(compgen -W "-n" -- "$cur") ) ;;
            dump) COMPREPLY=( $(compgen -W "-n --all" -- "$cur") ) ;;
            '') COMPREPLY=( $(compgen -W "$opts" -- "$cur") ) ;;
            *) COMPREPLY=() ;;
        esac
        return
    fi

    if [[ -n "$cmd" ]]; then
        return
    fi

    COMPREPLY=( $(compgen -W "$commands" -- "$cur") )
}
complete -F _tntctl tntctl
