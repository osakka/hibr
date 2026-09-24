class Hibr < Formula
  desc "Small, fast bash-flavoured shell with nested maps, JSON, and native networking"
  homepage "https://github.com/osakka/hibr"
  url "https://github.com/osakka/hibr/archive/c6f397cbc26f94cae50915c80402e88491e4edbf.tar.gz"
  version "0.21"
  sha256 "a4839fb06c244e50895d9ffddf74a3ee64d20747d7b276272d865b4740820603"
  license "MIT"
  head "https://github.com/osakka/hibr.git", branch: "main"

  # TLS support is dlopen'd against the system's libssl at first use, never
  # linked at build time -- so there is no openssl dependency to declare
  # here, on Homebrew or anywhere else.
  def install
    system "make", "PREFIX=#{prefix}"
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_equal "42", shell_output("#{bin}/hibr -c 'echo $((6 * 7))'").strip
    assert_match version.to_s, shell_output("#{bin}/hibr --version")
  end
end
