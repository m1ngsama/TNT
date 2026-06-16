# Installation Lifecycle

TNT core is the small Unix tool: text-first, compatible, and reliable. Modules
are downstream enhancements for modern terminal UX, visuals, automation, and
community features. Module failure must not take TNT down.

## Flow

1. Install `tnt` and `tntctl`.
2. Choose a profile: `core`, `select`, `all`, or `manual`.
3. Validate modules with `scripts/module_check.sh`.
4. Generate an env file with `scripts/install_wizard.sh`.
5. Review, install, restart, smoke-test.
6. Roll back by removing `TNT_MODULE_PATHS` and restarting.

## Commands

Generate a reviewable env file:

```sh
scripts/install_wizard.sh --output tnt.env
```

Preview all modules under a root:

```sh
scripts/install_wizard.sh --print-modules --module-root /opt/tnt-modules
```

Generate an all-valid-modules env file:

```sh
TNT_SETUP_PROFILE=all \
TNT_SETUP_MODULE_ROOT=/opt/tnt-modules \
scripts/install_wizard.sh --non-interactive --output tnt.env
```

Activate manually:

```sh
sudo install -m 600 tnt.env /etc/default/tnt
sudo systemctl restart tnt
ssh -p 2222 localhost health
```

The wizard never installs binaries, downloads modules, edits systemd units, or
restarts services. It only makes choices visible and emits config.
