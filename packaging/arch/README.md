# Arch / AUR Packaging

The draft package name is `tnt-chat` because `tnt` is already a likely name
collision in Arch/AUR contexts.

## Local validation

From this directory:

```sh
makepkg -si
```

Optional package linting:

```sh
namcap PKGBUILD
namcap tnt-chat-*.pkg.tar.zst
```

## Updating metadata

After editing `PKGBUILD`, regenerate `.SRCINFO`:

```sh
makepkg --printsrcinfo > .SRCINFO
```

Before AUR submission, replace `sha256sums=('SKIP')` with the real release
archive checksum, then run the project-level strict check:

```sh
make release-check-strict
```

## Manual AUR submission

```sh
git clone ssh://aur@aur.archlinux.org/tnt-chat.git aur-tnt-chat
cp PKGBUILD .SRCINFO aur-tnt-chat/
cd aur-tnt-chat
git add PKGBUILD .SRCINFO
git commit -m "Update to 1.0.1"
git push
```

Do not wire this to automatic deployment or release automation.
