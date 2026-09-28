class Hibr < Formula
  desc "Small, fast bash-flavoured shell with nested maps, JSON, and native networking"
  homepage "https://github.com/osakka/hibr"
  url "https://github.com/osakka/hibr/archive/refs/tags/v0.25.tar.gz"
  version "0.25"
  sha256 "7e2812555ff3a00df18c330f8bbb73cb7aaeec3ee0be08f0bcef79fc53cc58c7"
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

  # deploy.sh's own install path writes this same starter file; a plain
  # `brew install hibr` never runs deploy.sh at all, so without this hook
  # a Homebrew install got no default `.hibrc` and, since `command_not_found`
  # lives in it, no working autoloader either -- reported directly: a fresh
  # install with no `.hibrc` and `sysinfo` failing to autoload.
  def post_install
    rc = "#{Dir.home}/.hibrc"
    return if File.exist?(rc)
    begin
      File.write(rc, <<~RC)
        mod load prompt
        PROMPT[format]='$dir$git$duration$status$char'
        PROMPT[duration][min]=500

        alias ll='ls -lh'
        export EDITOR=vim

        # Autoload a module for a command it registers, once normal lookup
        # has already failed -- so `console key`, `img draw`, `darwin cpu`
        # and the rest of what a module offers work without an explicit
        # `mod load` or `need` first. Interactive only (.hibrc isn't read
        # by scripts), and only after PATH and every builtin/function has
        # already had first refusal, so it never shadows a real program
        # the way loading modules ahead of PATH would. Remove this
        # function, or return 127 unconditionally at its top, to go back
        # to requiring an explicit `need`/`mod load`.
        command_not_found() {
        	mod find "$1" > /dev/null 2>&1 && "$@" ||
        		{ echo "hibr: $1: command not found" >&2; return 127; }
        }
      RC
    rescue StandardError
      nil
    end
  end

  test do
    assert_equal "42", shell_output("#{bin}/hibr -c 'echo $((6 * 7))'").strip
    assert_match version.to_s, shell_output("#{bin}/hibr --version")
  end
end
