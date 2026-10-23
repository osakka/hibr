# Inside ${...} only another ${ nests: a bare { is a character and the
# first unescaped } ends the expansion, as in bash and dash (Gitea #361).
# hibr counted every { and refused the file with "unexpected end of
# input". And in a quoted :- := :? :+ word a backslash before a character
# the string does not treat as special is kept, as bash keeps it.
x=
y='a}b'
z='a*b'
echo "1 ${x:-{\}}"
echo "2 ${x:-\}}"
echo 3 ${x:-\}}
echo "4 ${y#*\}}"
echo "5 ${y/\}/Z}"
echo 6 ${y%\}*}
echo "7 ${x:-{a}b}"
echo 8 ${x:-{a}b}
echo "9 ${x:-{}}"
echo "10 ${x:-a{b}"
echo "11 ${x:-${x:-{}}}"
echo "12 ${x:-$(echo "}")}"
echo "13 ${x:-$(echo \})}"
echo "14 ${x:-$((1 + 2))}"
echo "15 ${x:-`echo }`}"
echo "16 ${x:-a\{b}" "${x:-a\zb}" "${x:-a\$b}" "${x:-a\"b}" "${x:-a\\b}"
echo 17 ${x:-a\zb} "${y:+a\zb}"
echo "18 ${z#a\*}" "${z/\*/Z}"
echo "19 ${x:=q\w}" "$x"
