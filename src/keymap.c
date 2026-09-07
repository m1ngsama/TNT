#include "keymap.h"

#include <strings.h>  /* strcasecmp */

tnt_keymap_t tnt_keymap_from_name(const char *name, tnt_keymap_t fallback) {
    if (!name) {
        return fallback;
    }
    if (strcasecmp(name, "vim") == 0) {
        return TNT_KEYMAP_VIM;
    }
    if (strcasecmp(name, "default") == 0) {
        return TNT_KEYMAP_DEFAULT;
    }
    return fallback;
}

tnt_keymap_t tnt_keymap_from_login(const char *ssh_login, tnt_keymap_t fallback) {
    if (!ssh_login) {
        return fallback;
    }
    /* Only an exact "vim" opts in.  "vimmer" is somebody's name, not a
     * request, so it must not change the interface under them. */
    if (strcasecmp(ssh_login, "vim") == 0) {
        return TNT_KEYMAP_VIM;
    }
    return fallback;
}

const char *tnt_keymap_name(tnt_keymap_t keymap) {
    return keymap == TNT_KEYMAP_VIM ? "vim" : "default";
}

bool tnt_keymap_uses_modes(tnt_keymap_t keymap) {
    return keymap == TNT_KEYMAP_VIM;
}
