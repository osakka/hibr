#include <stdio.h>
#include <string.h>
#include "hibr.h"
extern const char **si_logo(const char *id, const char *like);
extern const char **si_named(const char *id);
int main(void) {
  struct { const char *id, *like, *want; } t[] = {
    {"debian", "", "debian"}, {"ubuntu", "debian", "ubuntu"},
    {"linuxmint", "ubuntu debian", "mint"}, {"pop", "ubuntu debian", "ubuntu"},
    {"raspbian", "debian", "debian"}, {"arch", "", "arch"},
    {"manjaro", "arch", "arch"}, {"endeavouros", "arch", "arch"},
    {"fedora", "", "fedora"}, {"rocky", "rhel centos fedora", "rhel"},
    {"almalinux", "rhel centos fedora", "rhel"}, {"alpine", "", "alpine"},
    {"cix", "", "cix"}, {"nixos", "", "nixos"}, {"void", "", "void"},
    {"opensuse-leap", "suse opensuse", "opensuse"}, {"gentoo", "", "gentoo"},
    {"freebsd", "", "freebsd"}, {"weirdos", "", "hibr"},
    {"weirdos", "nonsense", "hibr"}, {"", "", "hibr"},
    {0,0,0}};
  int bad = 0, i;
  for (i = 0; t[i].id; i++) {
    const char **got = si_logo(t[i].id, t[i].like);
    const char **want = si_named(t[i].want);
    int ok = got == want;
    if (!ok) bad++;
    printf("%s %-14s like %-22s -> %s\n", ok ? "ok  " : "FAIL",
           t[i].id, t[i].like[0] ? t[i].like : "-", t[i].want);
  }
  printf("\n%s\n", bad ? "FAILURES" : "every identification lands on the right picture");
  return bad ? 1 : 0;
}
/* the pieces of the shell this one function does not need */
int u8dec(const char *p, size_t n, unsigned *cp) { (void)n; *cp = (unsigned char)*p; return 1; }
int u8w(unsigned c) { (void)c; return 1; }
const char *hibr_get(sh *s, const char *k) { (void)s; (void)k; return 0; }
