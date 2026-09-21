set -e
probe() {
  false
  echo "bash reaches here, hibr does not"
}
if probe; then echo yes; else echo no; fi
echo after
