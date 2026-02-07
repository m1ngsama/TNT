# TODO

## Maintenance
- [x] Replace deprecated `libssh` functions in `src/ssh_server.c`:
    - ~~`ssh_message_auth_password`~~ → `auth_password_function` callback (✓ completed)
    - ~~`ssh_message_channel_request_pty_width/height`~~ → `channel_pty_request_function` callback (✓ completed)
    - Migrated to callback-based server API as of libssh 0.9+

## Future Features
- [x] Implement robust command handling for non-interactive SSH exec requests.
    - Basic exec support completed (handles `exit` command)
    - All tests passing
