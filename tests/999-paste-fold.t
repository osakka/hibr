# A paste into a one-line field folds its lines (widgets/text.hibr): a line
# break becomes a space, so a name pasted over two lines stays two words --
# or, for a field drawn with the url rule, goes with the blanks around it, as
# a browser's address bar does: a terminal program that wrapped a long
# address put them there, and a space in the middle of an OAuth redirect is
# a refused sign-in (the owner's, 0.99.418).
. programs/desktop/widgets/text.hibr
r := dt_pastefold $'Pat\nDoe'
printf '[%s]\n' "$r"
r := dt_pastefold $'a\tb\r\nc'
printf '[%s]\n' "$r"
r := dt_pastefold $'https://claude.ai/oauth?redirect=https%3A%2F%2Fx%2Foauth%2Fcode%2  \r\n  Fcallback&state=1' url
printf '[%s]\n' "$r"
r := dt_pastefold $'one\n\n\ntwo three\tfour' url
printf '[%s]\n' "$r"
r := dt_pastefold $'  \n  ' url
printf '[%s]\n' "$r"
