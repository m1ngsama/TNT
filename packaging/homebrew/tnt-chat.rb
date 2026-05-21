class TntChat < Formula
  desc "SSH-native terminal chat server with a Vim-style interface"
  homepage "https://github.com/m1ngsama/TNT"
  url "https://github.com/m1ngsama/TNT/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "REPLACE_WITH_RELEASE_TARBALL_SHA256"
  license "MIT"

  depends_on "libssh"

  def install
    system "make"
    system "make", "install", "DESTDIR=#{buildpath}/stage", "PREFIX=#{prefix}"

    bin.install "#{buildpath}/stage#{prefix}/bin/tnt"
    man1.install "#{buildpath}/stage#{prefix}/share/man/man1/tnt.1"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/tnt --version")
  end
end
