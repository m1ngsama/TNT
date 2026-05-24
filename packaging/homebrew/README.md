# Homebrew Packaging

The draft formula is `tnt-chat.rb`. The expected install path for users is a
project tap first, not Homebrew core:

```sh
brew tap m1ngsama/tnt
brew install tnt-chat
```

Homebrew core should wait until TNT has stable releases and broader usage.

## Local validation

From a tap repository:

```sh
brew audit --strict --online tnt-chat
brew install --build-from-source ./Formula/tnt-chat.rb
brew test tnt-chat
```

For local syntax-only validation from this repository:

```sh
ruby -c packaging/homebrew/tnt-chat.rb
```

## Updating the formula

1. Publish a GitHub release tag such as `v1.0.1`.
2. Download or hash the release source archive:

   ```sh
   curl -L -o tnt-chat-1.0.1.tar.gz \
     https://github.com/m1ngsama/TNT/archive/refs/tags/v1.0.1.tar.gz
   shasum -a 256 tnt-chat-1.0.1.tar.gz
   ```

3. Replace `REPLACE_WITH_RELEASE_TARBALL_SHA256` in `tnt-chat.rb`.
4. Run:

   ```sh
   make release-check-strict
   ```

5. Copy the formula into the tap repository and open a normal review PR.

Do not connect this tap update to production deployment.
