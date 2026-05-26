# Packaging

This directory contains package-manager drafts for TNT. They are intentionally
kept out of the root install path and should be reviewed before submission to
any public registry.

## Current targets

- `arch/` - AUR-ready draft for `tnt-chat`.
- `homebrew/` - Homebrew tap formula draft and maintainer notes.
- `debian/` - Ubuntu PPA / Debian packaging notes and draft metadata.

Package installs include both `tnt` and `tntctl`.  `tnt` is the server process;
`tntctl` is a thin wrapper around the documented SSH exec interface.

## Release checklist

1. Confirm `TNT_VERSION` in `include/common.h` and the manpage version match.
   Also update package versions in Arch, Homebrew, and Debian drafts.
2. Create a GitHub release tag such as `v1.0.1`.
3. Build and upload release tarballs or rely on GitHub source archives.
4. Replace placeholder checksums in package drafts.
5. Verify package contents in an isolated directory:

   ```sh
   make release-check
   ```

6. Before submitting package recipes, replace checksum placeholders and run:

   ```sh
   make release-check-strict
   ```

7. Submit packages manually:
   - Arch: upload `PKGBUILD` and generated `.SRCINFO` to AUR.
   - Homebrew: open a PR to the project tap, or later Homebrew core if eligible.
   - Ubuntu: build Debian source packages and upload to a Launchpad PPA.

Do not connect these packaging drafts to automatic production deployment.
