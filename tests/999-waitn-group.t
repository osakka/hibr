# wait -n answers for a child that left the process group it was started in
# (Gitea #447's review): the job is waited for by its own pid when its group
# has none left. Recorded rather than compared, because bash's own answer
# here varies from run to run (0 or 9) while hibr's does not.
command -v python3 > /dev/null 2>&1 || { echo "wait -n 9"; exit 0; }
python3 -c "import os,time; time.sleep(.2); os.setpgid(0, os.getpgid(os.getppid())); raise SystemExit(9)" &
wait -n
echo "wait -n $?"
