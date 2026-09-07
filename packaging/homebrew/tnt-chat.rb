class TntChat < Formula
  desc "SSH-native terminal chat server with a Vim-style interface"
  homepage "https://github.com/m1ngsama/TNT"
  url "https://github.com/m1ngsama/TNT/releases/download/v1.3.1/tnt-chat-v1.3.1-source.tar.gz"
  sha256 "REPLACE_WITH_RELEASE_TARBALL_SHA256"
  license "MIT"

  depends_on "libssh"

  def install
    system "make"
    system "make", "install", "DESTDIR=#{buildpath}/stage", "PREFIX=#{prefix}"

    bin.install "#{buildpath}/stage#{prefix}/bin/tnt"
    bin.install "#{buildpath}/stage#{prefix}/bin/tntctl"
    man1.install "#{buildpath}/stage#{prefix}/share/man/man1/tntctl.1"
    man5.install "#{buildpath}/stage#{prefix}/share/man/man5/tnt-message-log.5"
    man7.install "#{buildpath}/stage#{prefix}/share/man/man7/tnt-chat.7"
    man7.install "#{buildpath}/stage#{prefix}/share/man/man7/tnt-exec.7"
    man7.install "#{buildpath}/stage#{prefix}/share/man/man7/tnt-module-protocol.7"
    man8.install "#{buildpath}/stage#{prefix}/share/man/man8/tnt.8"
    bash_completion.install \
      "#{buildpath}/stage#{prefix}/share/bash-completion/completions/tntctl"
    zsh_completion.install \
      "#{buildpath}/stage#{prefix}/share/zsh/site-functions/_tntctl"
    fish_completion.install \
      "#{buildpath}/stage#{prefix}/share/fish/vendor_completions.d/tntctl.fish"

    (var/"tnt").mkpath
    (var/"log").mkpath
  end

  service do
    run [opt_bin/"tnt", "-d", var/"tnt"]
    keep_alive true
    working_dir var/"tnt"
    log_path var/"log/tnt.log"
    error_log_path var/"log/tnt.log"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/tnt --version")
    assert_match version.to_s, shell_output("#{bin}/tntctl --version")
  end
end
