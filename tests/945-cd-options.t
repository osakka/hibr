# cd takes -L, -P and -- before the directory, as bash's does
cd -- /tmp && pwd
cd -P / && pwd
cd -L /usr && pwd
cd -LP /tmp && pwd
cd -
cd -x 2> /dev/null
echo "a bad option: $?"
mkdir -p /tmp/hibr-cd-$$/-n
cd /tmp/hibr-cd-$$ && cd -- -n && pwd | sed 's/[0-9]*\/-n$/N\/-n/'
cd /
rm -rf /tmp/hibr-cd-$$
