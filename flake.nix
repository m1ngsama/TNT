{
  description = "TNT: anonymous SSH chat server, plus the WebSocket gateway module";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";

  outputs =
    { self, nixpkgs }:
    let
      forSystem = system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          version = self.shortRev or "dirty";
          tnt = pkgs.stdenv.mkDerivation {
            pname = "tnt";
            inherit version;
            src = self;
            buildInputs = [ pkgs.libssh ];
            makeFlags = [ "tnt" "tntctl" ];
            installPhase = ''
              install -Dm755 tnt tntctl -t $out/bin
              install -Dm644 tntctl.1 -t $out/share/man/man1
              install -Dm644 tnt-message-log.5 -t $out/share/man/man5
              install -Dm644 tnt-chat.7 tnt-exec.7 tnt-module-protocol.7 -t $out/share/man/man7
              install -Dm644 tnt.8 -t $out/share/man/man8
            '';
            meta.mainProgram = "tnt";
          };
          # bun's node_modules as a fixed-output derivation; bump the hash when gateway/bun.lock changes
          gatewayDeps = pkgs.stdenvNoCC.mkDerivation {
            name = "tnt-gateway-deps";
            src = ./gateway;
            nativeBuildInputs = [ pkgs.bun ];
            dontConfigure = true;
            buildPhase = ''
              export HOME=$TMPDIR
              bun install --frozen-lockfile --no-progress --ignore-scripts --no-cache --production
            '';
            installPhase = "cp -R node_modules $out";
            dontFixup = true;
            outputHashMode = "recursive";
            outputHash = "sha256-RdWqoEj6WIvmoyrjPAF1Mj7U/ovn945YMgECnJz+G9E=";
          };
          # The module directory TNT loads: tnt-module.json next to an executable entrypoint. Instead of a
          # 100 MB `bun build --compile` binary, the entrypoint runs the TypeScript sources under bun.
          gateway = pkgs.stdenvNoCC.mkDerivation {
            pname = "tnt-gateway";
            inherit version;
            src = ./gateway;
            installPhase = ''
              mkdir -p $out
              cp -R src tnt-module.json $out/
              ln -s ${gatewayDeps} $out/node_modules
              cat > $out/tnt-gateway <<SH
              #!${pkgs.runtimeShell}
              exec ${pkgs.bun}/bin/bun run $out/src/main.ts "\$@"
              SH
              chmod 755 $out/tnt-gateway
            '';
          };
        in
        { default = tnt; inherit tnt gateway; };
    in
    {
      packages = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ] forSystem;
    };
}
