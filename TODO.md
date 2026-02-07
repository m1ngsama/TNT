# TODO

## Maintenance
- [ ] Replace deprecated `libssh` functions in `src/ssh_server.c`:
    - `ssh_message_auth_password` (deprecated in newer libssh)
    - `ssh_message_channel_request_pty_width`
    - `ssh_message_channel_request_pty_height`

## Future Features
- [ ] Implement robust command handling for non-interactive SSH exec requests.
