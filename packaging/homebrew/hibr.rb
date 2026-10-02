class Hibr < Formula
  desc "Small, fast bash-flavoured shell with nested maps, JSON, and native networking"
  homepage "https://github.com/osakka/hibr"
  url "https://github.com/osakka/hibr/archive/refs/tags/v0.92.tar.gz"
  version "0.92"
  sha256 "eef73bb246368047b4a3210a787bb9a106a648966df654deb170f8c4d830c4f2"
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

  # Homebrew formulas do not write into a user's home directory -- not a
  # limitation to work around, a deliberate one to respect: two formulas
  # could collide on one dotfile, an uninstall would have to decide
  # whether to touch a file it did not create, and an upgrade could
  # clobber whatever the user had customised. post_install tried this
  # anyway, through two revisions, and never reliably ran even once on
  # real hardware -- confirmed why, eventually: `post_install` itself is
  # deprecated in current Homebrew ("Warning: Calling `post_install` is
  # deprecated! Use `post_install_steps` instead", seen live), and its
  # declarative replacement only ever writes within the formula's own
  # prefix (base: :etc/:bin/:libexec/:homebrew_prefix) -- there is no
  # :home, by design, for the reasons above. caveats is the right tool
  # for this instead: always shown, never deprecated, and it asks rather
  # than acts.
  def caveats
    <<~EOS
      hibr doesn't read /etc/profile or similar -- ~/.hibrc, read by every
      interactive shell (see hibr -d 2 if you want to confirm it's being
      looked for). Add this to it for a module's command to autoload the
      first time you type it, without an explicit `mod load`/`need` first:

        command_not_found() {
        	mod find "$1" > /dev/null 2>&1 && "$@" ||
        		{ echo "hibr: $1: command not found" >&2; return 127; }
        }
    EOS
  end

  test do
    assert_equal "42", shell_output("#{bin}/hibr -c 'echo $((6 * 7))'").strip
    assert_match version.to_s, shell_output("#{bin}/hibr --version")
  end
end
