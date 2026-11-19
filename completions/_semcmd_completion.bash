_semcmd() {
    local cur prev opts commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    commands="create lock unlock destroy infos list clean"
    opts="-h --help -V --version -f --force -l --locked"
    
    case "${prev}" in
        create|lock|unlock|destroy|infos)
            # Completion for existing lock names
            local locks=$(semcmd list 2>/dev/null | grep -oP '^\W*- \K[\w-]+(?= )')
            COMPREPLY=($(compgen -W "${locks}" -- ${cur}))
            return 0
            ;;
        *)
            ;;
    esac
    
    if [[ ${cur} == -* ]]; then
        COMPREPLY=($(compgen -W "${opts}" -- ${cur}))
    else
        COMPREPLY=($(compgen -W "${commands}" -- ${cur}))
    fi
}

complete -F _semcmd semcmd