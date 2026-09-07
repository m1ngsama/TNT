#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>

/* Which set of keys a session uses.
 *
 * The default keymap never enters a mode: typing always types.  The vim keymap
 * is the historical interface, with INSERT, NORMAL, and COMMAND.  TNT is
 * anonymous and stores no per-user state, so a session picks its keymap from
 * the SSH login name or a command, and the operator picks the server-wide
 * default. */
typedef enum {
    TNT_KEYMAP_DEFAULT = 0,
    TNT_KEYMAP_VIM
} tnt_keymap_t;

/* Accepts "default" and "vim".  Any other value, including NULL, returns the
 * fallback so a bad configuration never silently changes the interface. */
tnt_keymap_t tnt_keymap_from_name(const char *name, tnt_keymap_t fallback);

/* An SSH login name of "vim" selects the vim keymap.  The login name selects
 * neither identity nor nickname in TNT, which is what leaves it free to carry
 * this signal; users put it in ssh_config once. */
tnt_keymap_t tnt_keymap_from_login(const char *ssh_login, tnt_keymap_t fallback);

const char *tnt_keymap_name(tnt_keymap_t keymap);

/* True only for the vim keymap.  Guards every mode transition. */
bool tnt_keymap_uses_modes(tnt_keymap_t keymap);

#endif /* KEYMAP_H */
