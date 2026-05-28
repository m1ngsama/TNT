# Homebrew Packaging

The draft formula is `tnt-chat.rb`. The expected install path for users is a
project tap first, not Homebrew core:

```sh
brew tap m1ngsama/tnt
brew install tnt-chat
brew services start tnt-chat
```

Homebrew core should wait until TNT has stable releases and broader usage.

## Local validation

From a tap repository:

```sh
brew audit --strict --online tnt-chat
brew install --build-from-source ./Formula/tnt-chat.rb
brew test tnt-chat
brew services run tnt-chat
```

For local syntax-only validation from this repository:

```sh
ruby -c packaging/homebrew/tnt-chat.rb
```

## Updating the formula

1. Publish a GitHub release tag such as `vX.Y.Z`.
2. Download or hash the release source archive:

   ```sh
   curl -L -o dist/tnt-chat-vX.Y.Z-source.tar.gz \
     https://github.com/m1ngsama/TNT/releases/download/vX.Y.Z/tnt-chat-vX.Y.Z-source.tar.gz
   shasum -a 256 dist/tnt-chat-vX.Y.Z-source.tar.gz
   ```

3. Replace `REPLACE_WITH_RELEASE_TARBALL_SHA256` in `tnt-chat.rb`.
4. Run:

   ```sh
   SOURCE_TARBALL=dist/tnt-chat-vX.Y.Z-source.tar.gz make package-publish-check
   ```

5. Copy the formula into the tap repository and open a normal review PR.

Do not connect this tap update to production deployment.
