# The grammar

What the parser actually accepts, taken from `src/lex.c` and `src/parse.c`
rather than from memory. Where hibr differs from POSIX or bash the difference is
marked **[hibr]** and points at the decision record that explains it.

- [Lexical structure](#lexical-structure)
- [Grammar](#grammar)
- [Operator precedence](#operator-precedence)
- [Expansion](#expansion)
- [Patterns](#patterns)

## Lexical structure

The lexer produces one token at a time. A **word** is the interesting one: it is
a list of *parts*, each carrying a flag saying whether its bytes were quoted.
That per-byte record survives expansion, which is what lets splitting, globbing
and subscripts each ask a different question about the same text.

### Tokens

| token | spelling |
|---|---|
| word | any run of characters not ended by a metacharacter |
| operator | `\| \|\| & && ; ;; ;& ;;& ( ) \|& < > >> <& >& <> <<< << <<-` |
| assignment | `name=value` or `name+=value`, only before the command word |
| newline | ends a list; a `\` before it continues the line |
| comment | `#` to end of line, when `#` starts a word |

A metacharacter — space, tab, newline, `| & ; < > ( )` — ends an unquoted word.
One exception: `?( *( +( @( !(` do not, because they open an extended pattern
that belongs to the word. **[hibr]** bash needs `shopt -s extglob` on an
*earlier line* for that, since the option is read at parse time; here there is
nothing to switch on — [0017](adr/0017-one-namespace-for-options.md).

### Quoting

| form | effect |
|---|---|
| `\c` | the single character `c`, literally; `\` before a newline joins lines |
| `'…'` | every byte literal, no escapes, cannot contain `'` |
| `"…"` | literal except `$` `` ` `` `\` and, inside, `!` when history expansion is on |
| `$'…'` | ANSI-C escapes: `\n \t \r \a \b \f \v \e \\ \' \xHH \0NNN` |

### Reserved words

`!` `[[` `]]` `{` `}` `case` `do` `done` `elif` `else` `esac` `fi` `for` `fn`
`if` `in` `select` `then` `until` `while`

They are only reserved where a command word may start. `esac` closes a `case`
arm even with no `;;` before it, and `echo esac` prints `esac`.

**[hibr]** `fn` declares a function with typed parameters and a return type. The
POSIX `name() { … }` form works too.

## Grammar

EBNF. `{ x }` is zero or more, `[ x ]` optional, `|` alternation.

```ebnf
program     = list ;

list        = and_or { ( ";" | "&" | newline ) [ and_or ] } ;

and_or      = pipeline { ( "&&" | "||" ) [ newline ] pipeline } ;

pipeline    = [ "!" ] command { ( "|" | "|&" ) [ newline ] command } ;

command     = compound [ redirect_list ]
            | simple ;

simple      = { assignment } word { word | redirect } ;

compound    = subshell | group  | if_cl | loop  | for_cl
            | select | case_cl | cond  | arith | function ;

subshell    = "(" list ")" ;
group       = "{" list "}" ;

if_cl       = "if" list "then" list
              { "elif" list "then" list } [ "else" list ] "fi" ;

loop        = ( "while" | "until" ) list "do" list "done" ;

for_cl      = "for" name [ "in" { word } ] ( ";" | newline ) "do" list "done"
            | "for" "((" [ arith ] ";" [ arith ] ";" [ arith ] "))"
              "do" list "done" ;

select      = "select" name [ "in" { word } ] ( ";" | newline )
              "do" list "done" ;

case_cl     = "case" word "in" { case_arm } "esac" ;
case_arm    = [ "(" ] pattern { "|" pattern } ")" [ list ]
              [ ";;" | ";&" | ";;&" ] ;

cond        = "[[" cond_expr "]]" ;
arith       = "((" arith_expr "))" ;

function    = "fn" name "(" [ params ] ")" [ "->" type ] "{" list "}"
            | name "(" ")" compound ;
params      = param { "," param } ;
param       = [ type ] name [ "=" word ] ;
type        = "int" | "num" | "str" | "path" | "arr" | "map" | "any" ;

assignment  = name [ "[" subscript "]" ] ( "=" | "+=" ) word
            | name "=" "(" { word | "[" subscript "]" "=" word } ")" ;

redirect    = [ number | "{" name "}" ] redirect_op word ;
redirect_op = "<" | ">" | ">>" | ">|" | "<&" | ">&" | "<>"
            | "<<" | "<<-" | "<<<" ;
```

The last arm of a `case` may omit its `;;`, as POSIX allows. `;&` falls through
to the next arm's body; `;;&` re-tests from the next arm.

### Redirections

| form | meaning |
|---|---|
| `< f` `> f` `>> f` | read, truncate, append |
| `>\| f` | truncate even under `noclobber` |
| `n< f` `n> f` | the same on descriptor `n` |
| `{v}< f` | open on a free descriptor above 9 and put its number in `v` |
| `<& n` `>& n` | duplicate; `<&-` and `>&-` close |
| `<> f` | open for reading and writing |
| `<<< word` | the word, with a newline, as standard input |
| `<< T` `<<- T` | here document to the line `T`; `<<-` strips leading tabs |

A quoted here-document delimiter (`<<'T'`) stops expansion of the body.
`noclobber` guards only regular files, so `2>/dev/null` keeps working.

**[hibr]** Any redirection target may be a scheme: `/dev/tcp/host/port`,
`/dev/udp/…`, `/dev/tls/…`, `/dev/unix/path`, and whatever a module registers —
see [Networking](networking.md) and [Modules](modules.md).

## Operator precedence

### Arithmetic

Loosest first. From `ax_tk` and `ax_pr` in `src/expand.c`.

| level | operators | associativity |
|---|---|---|
| 1 | `,` | left |
| 2 | `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `^=` `\|=` | right |
| 3 | `?:` | right |
| 4 | `\|\|` | left, short-circuits |
| 5 | `&&` | left, short-circuits |
| 6 | `\|` | left |
| 7 | `^` | left |
| 8 | `&` | left |
| 9 | `==` `!=` | left |
| 10 | `<` `<=` `>` `>=` | left |
| 11 | `<<` `>>` | left |
| 12 | `+` `-` | left |
| 13 | `*` `/` `%` | left |
| 14 | `**` | **right** |
| 15 | unary `-` `+` `!` `~` `++` `--` | right |
| 16 | `( )`, numbers, names, `name[sub]` | — |

A number is decimal, `0x…` hexadecimal, `0…` octal, or `base#digits` for any
base from 2 to 64. Above base 36 case is significant, and `@` and `_` are 62 and
63. Names are read without a `$`; `a[i]` reads an element.

Overflow is computed in unsigned and cast back, so it wraps rather than being
undefined, and `INT64_MIN / -1` is special-cased. Division by zero is an error.

**[hibr]** `((expr))` never trips `set -e`: its status is a value, not a
failure — [0003](adr/0003-arithmetic-status-is-a-value.md).

### Conditional expressions

`[[ … ]]`, loosest first: `||`, then `&&`, then `!`, then `( )` and primaries.
Unlike `[`, no expansion splitting happens inside, so quoting is rarely needed.

| unary | true when |
|---|---|
| `-z s` `-n s` | the string is empty, is not empty |
| `-e f` | the path exists |
| `-f f` `-d f` | it is a regular file, a directory |
| `-r f` `-w f` `-x f` | it is readable, writable, executable |
| `-s f` | it exists and is not empty |
| `-L f` `-h f` | it is a symbolic link |
| `-v n` | the variable is set |

| binary | true when |
|---|---|
| `==` `=` | the left matches the right **as a pattern** |
| `!=` | it does not |
| `=~` | the left matches the right as a POSIX extended regex |
| `<` `>` | string comparison |
| `-eq -ne -lt -le -gt -ge` | numeric comparison |
| `-nt` `-ot` `-ef` | newer, older, the same file |

**[hibr]** `=~` captures into `M`, not `BASH_REMATCH`: `${M[0]}` is the whole
match and `${M[1]}` the first group —
[0004](adr/0004-regex-captures-go-to-M.md).

## Expansion

A word is expanded in this order. Each step sees the per-byte quote record, so a
quoted byte is never split, globbed, or read as an operator.

1. **Brace expansion** — `{a,b}`, `{1..9}`, `{01..12}`, `{a..e}`, `{1..9..2}`,
   nested and multiplied. **[hibr]** literal only: `{$a,$b}` is not expanded —
   [0007](adr/0007-brace-expansion-is-literal.md).
2. **Tilde** — `~`, `~user`, `~+`, `~-`, `~N`.
3. **Parameter, command, arithmetic and process substitution**, left to right in
   one pass: `$name`, `${…}`, `$(…)`, `` `…` ``, `$((…))`, `<(…)`, `>(…)`.
4. **Word splitting** on `IFS`, over unquoted bytes only.
5. **Pathname expansion**, over unquoted bytes only.
6. **Quote removal**.

Steps 4 and 5 are skipped where one word is required — an assignment's value, a
`case` subject, the right of `=~`, and everything under `set -S`
([0009](adr/0009-strict-expansion-is-opt-in.md)).

### Parameter expansion

| form | gives |
|---|---|
| `${x}` | the value |
| `${x:-d}` `${x:=d}` `${x:?m}` `${x:+a}` | default, assign, error, alternative |
| `${#x}` | length; `${#a[@]}` the number of elements |
| `${x#p}` `${x##p}` `${x%p}` `${x%%p}` | strip shortest or longest prefix, suffix |
| `${x/p/r}` `${x//p/r}` | replace first, replace all |
| `${x/#p/r}` `${x/%p/r}` | replace only at the start, at the end |
| `${x:off:len}` | substring; a negative offset counts from the end |
| `${x^} ${x^^} ${x,} ${x,,}` | upper or lower the first byte, or all |
| `${x@Q}` | quoted so reading it back gives the same string |
| `${x@E}` | with backslash escapes expanded |
| `${x@U} ${x@L} ${x@u}` | upper, lower, first byte upper |
| `${!ref}` | the value of the variable *named* by `x`, subscripts and all, keeping any modifier |
| `${!pre*}` `${!pre@}` | the names that begin with `pre`, sorted |
| `${a[k]}` | one element; `${a[@]}` all, `${a[*]}` joined |
| `${!a[@]}` | the keys |
| `${a[-1]}` | counted back from the **highest key**, so sparse arrays agree |

Dropping the `:` in the first row tests only whether the name is set, not
whether it is also non-empty.

**[hibr]** A **quoted subscript is a literal key**: `h["content-type"]` is the
key `content-type`, while `h[content-type]` is still arithmetic and lands on
`0` — [0006](adr/0006-arrays-are-sparse-maps.md).

## Patterns

| form | matches |
|---|---|
| `*` | any run of characters, not `/` in a pathname |
| `?` | one character |
| `[abc]` `[a-z]` `[!a-z]` `[^a-z]` | one of, a range, not one of |
| `**` | any depth of directories |
| `?(p)` | zero or one of `p` |
| `*(p)` | zero or more |
| `+(p)` | one or more |
| `@(p)` | exactly one |
| `!(p)` | anything that is not `p` |

`p` inside a group is one or more patterns separated by `|`. Groups nest.

**[hibr]** `**` is always on and never follows a symbolic link
([0008](adr/0008-globstar-is-always-on.md)); the extended groups are always on
too ([0017](adr/0017-one-namespace-for-options.md)).

---

[← documentation index](README.md)
