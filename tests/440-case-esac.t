case z in *) echo "last arm needs no double semicolon"
esac

case w in a) echo A ;; *) echo W ;; esac

case q in
  start)
  ;;
  stop|reload)
  echo stopping
  ;;
  *)
  echo usage
esac

case n in *) echo outer; case m in *) echo inner
esac
esac

echo esac
x=esac; echo "$x"
for i in esac done fi; do echo "word $i"; done

f() {
  case "$1" in
    a) echo fn-a ;;
    *) echo fn-other
  esac
}
f a
f b
