_semcmd() {
    local cur word cmd="" has_name=0 opts="" i
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"

    # The command is the first word that is not an option, the lock name the second
    for ((i = 1; i < COMP_CWORD; i++)); do
        word="${COMP_WORDS[i]}"
        case "${word}" in
            -h|--help|-V|--version)
                return 0
                ;;
            -*)
                ;;
            *)
                if [[ -z ${cmd} ]]; then
                    cmd="${word}"
                else
                    has_name=1
                fi
                ;;
        esac
    done

    # Before the command: only help, version and the commands
    if [[ -z ${cmd} ]]; then
        if [[ ${cur} == -* ]]; then
            COMPREPLY=($(compgen -W "-h --help -V --version" -- "${cur}"))
        else
            COMPREPLY=($(compgen -W "create lock unlock destroy infos list clean" -- "${cur}"))
        fi
        return 0
    fi

    # After the command: its own option, unless already given
    if [[ ${cur} == -* ]]; then
        case "${cmd}" in
            create)        opts="-l --locked" ;;
            destroy|clean) opts="-f --force" ;;
        esac
        for ((i = 1; i < COMP_CWORD; i++)); do
            [[ " ${opts} " == *" ${COMP_WORDS[i]} "* ]] && opts=""
        done
        COMPREPLY=($(compgen -W "${opts}" -- "${cur}"))
        return 0
    fi

    # Existing lock names (not for create, which needs a new name)
    case "${cmd}" in
        lock|unlock|destroy|infos)
            if (( ! has_name )); then
                local locks=$(semcmd list 2>/dev/null | grep -oP '^\W*- \K[\w-]+(?= )')
                COMPREPLY=($(compgen -W "${locks}" -- "${cur}"))
            fi
            ;;
    esac
}

complete -F _semcmd semcmd
