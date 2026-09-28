class Hibr < Formula
  desc "Small, fast bash-flavoured shell with nested maps, JSON, and native networking"
  homepage "https://github.com/osakka/hibr"
  url "https://github.com/osakka/hibr/archive/refs/tags/v0.24.tar.gz"
  version "0.24"
  sha256 "c08bc7fed9895a22073cddd3f09efe9396df996b5c663995d2c8dc515cac0986"
  license "MIT"
  head "https://github.com/osakka/hibr.git", branch: "main"

  # TLS and PNG support are dlopen'd at first use, never linked at build
  # time -- on Linux that reaches the system's own libssl/libpng, so no
  # dependency is needed there. macOS is different: its own unversioned
  # system libssl/libpng are not third-party-loadable at all past a
  # point (dyld hard-aborts on them, confirmed against Apple's own
  # developer forums), so hibr looks for Homebrew's versioned builds by
  # full path there instead, and needs them actually installed to find.
  on_macos do
    depends_on "libpng"
    depends_on "openssl@3"
  end

  def install
    system "make", "PREFIX=#{prefix}"
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_equal "42", shell_output("#{bin}/hibr -c 'echo $((6 * 7))'").strip
    assert_match version.to_s, shell_output("#{bin}/hibr --version")
  end
end
