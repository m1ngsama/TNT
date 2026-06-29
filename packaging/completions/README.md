# tntctl shell completions

Tab-completion for the `tntctl` control client: option flags, the stable
subcommands (`health`, `stats`, `users`, `tail`, `dump`, `post`, `help`,
`exit`), `--json` after `users`/`stats`, and the `--host-key-checking` modes.

These complete `tntctl`'s own interface only; the remote host argument is
free-form and is not completed.

## bash

```sh
# user-local
echo 'source /path/to/TNT/packaging/completions/tntctl.bash' >> ~/.bashrc
# or system-wide
sudo cp packaging/completions/tntctl.bash \
    /usr/share/bash-completion/completions/tntctl
```

## zsh

```sh
mkdir -p ~/.zsh/completions
cp packaging/completions/_tntctl ~/.zsh/completions/_tntctl
# ensure these are in ~/.zshrc:
#   fpath=(~/.zsh/completions $fpath)
#   autoload -U compinit && compinit
```

## fish

```sh
cp packaging/completions/tntctl.fish ~/.config/fish/completions/tntctl.fish
```

> Note: this is shell completion for the `tntctl` command line. Inside the
> interactive TNT chat (`ssh -p 2222 host`), COMMAND mode has its own built-in
> Tab completion for `:` commands and their arguments.
