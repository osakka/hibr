#include "vi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

int fails;
void ck(const char *n, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", n); if (!ok) fails++; }

char *txt(vi_buf *b) {
  static str o; o.n = 0; if (o.p) o.p[0] = 0;
  vi_get(b, 0, vi_len(b), &o);
  if (!o.p) { o.p = xm(1); o.p[0] = 0; }
  o.p[o.n] = 0; return o.p;
}

int main(void) {
  vi_buf b; size_t i; clock_t t0; double ms;
  vi_binit(&b);
  ck("empty length", vi_len(&b) == 0);
  ck("empty has one line", vi_nlines(&b) == 1);

  vi_ins(&b, 0, "hello", 5);
  ck("insert", !strcmp(txt(&b), "hello") && vi_len(&b) == 5);
  vi_ins(&b, 5, " world", 6);
  ck("append", !strcmp(txt(&b), "hello world"));
  vi_ins(&b, 0, ">> ", 3);
  ck("insert at front", !strcmp(txt(&b), ">> hello world"));
  vi_del(&b, 0, 3);
  ck("delete at front", !strcmp(txt(&b), "hello world"));
  vi_del(&b, 5, 6);
  ck("delete at end", !strcmp(txt(&b), "hello"));

  vi_bfree(&b); vi_binit(&b);
  vi_ins(&b, 0, "one\ntwo\nthree\n", 14);
  ck("three lines", vi_nlines(&b) == 3);
  ck("line 0", vi_lstart(&b,0) == 0 && vi_lend(&b,0) == 3);
  ck("line 1", vi_lstart(&b,1) == 4 && vi_lend(&b,1) == 7);
  ck("line 2", vi_lstart(&b,2) == 8 && vi_lend(&b,2) == 13);
  ck("lineof", vi_lineof(&b,0)==0 && vi_lineof(&b,5)==1 && vi_lineof(&b,9)==2);

  /* an edit in the middle must fix the index, not just the text */
  vi_ins(&b, 4, "X\n", 2);
  ck("index after a mid insert", vi_nlines(&b) == 4);
  ck("line 2 moved", vi_lstart(&b,2) == 6);
  vi_del(&b, 4, 2);
  ck("index after a mid delete", vi_nlines(&b) == 3 && vi_lstart(&b,2) == 8);

  /* utf-8 steps by character */
  vi_bfree(&b); vi_binit(&b);
  vi_ins(&b, 0, "a\xc3\xa9\xe6\xbc\xa2z", 8);
  i = 0;
  i = vi_next(&b, i); ck("next over ascii", i == 1);
  i = vi_next(&b, i); ck("next over two-byte", i == 3);
  i = vi_next(&b, i); ck("next over three-byte", i == 6);
  i = vi_prev(&b, i); ck("prev over three-byte", i == 3);
  i = vi_prev(&b, i); ck("prev over two-byte", i == 1);

  /* a big file, edited at the far end */
  vi_bfree(&b); vi_binit(&b);
  {
    size_t n = 50u*1024*1024, k;
    char *big = xm(n);
    for (k = 0; k < n; k++) big[k] = (k % 64 == 63) ? '\n' : 'x';
    t0 = clock();
    vi_ins(&b, 0, big, n);
    ms = (double)(clock()-t0)*1000/CLOCKS_PER_SEC;
    printf("     50 MB load: %.0f ms\n", ms);
    t0 = clock();
    ck("50 MB line count", vi_nlines(&b) == n/64);
    ms = (double)(clock()-t0)*1000/CLOCKS_PER_SEC;
    printf("     first index: %.0f ms\n", ms);
    t0 = clock();
    for (k = 0; k < 200; k++) { vi_ins(&b, vi_len(&b), "!", 1); vi_nlines(&b); }
    ms = (double)(clock()-t0)*1000/CLOCKS_PER_SEC;
    printf("     200 edits at the end, re-indexed each time: %.0f ms\n", ms);
    ck("edits at the end stay cheap", ms < 50);
    free(big);
  }
  vi_bfree(&b);
  printf("\n%s\n", fails ? "FAILURES" : "all buffer checks passed");
  return fails ? 1 : 0;
}
