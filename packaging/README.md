# Packaging

This directory contains package-manager drafts for TNT. They are intentionally
kept out of the root install path and should be reviewed before submission to
any public registry.

## Current targets

- `arch/PKGBUILD` - AUR-ready draft for `tnt-chat`.
- `homebrew/tnt-chat.rb` - Homebrew tap formula draft.
- `debian/README.md` - Ubuntu PPA / Debian packaging notes.

## Release checklist

1. Confirm `TNT_VERSION` in `include/common.h` and the manpage version match.
2. Create a GitHub release tag such as `v1.0.0`.
3. Build and upload release tarballs or rely on GitHub source archives.
4. Replace placeholder checksums in package drafts.
5. Verify package contents in an isolated directory:

   ```sh
   make clean
   make
   tmpdir="$(mktemp -d)"
   make DESTDIR="$tmpdir" PREFIX=/usr install
   find "$tmpdir" -type f | sort
   ```

6. Submit packages manually:
   - Arch: upload `PKGBUILD` and generated `.SRCINFO` to AUR.
   - Homebrew: open a PR to the project tap, or later Homebrew core if eligible.
   - Ubuntu: build Debian source packages and upload to a Launchpad PPA.

Do not connect these packaging drafts to automatic production deployment.
