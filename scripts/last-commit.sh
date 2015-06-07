#!/bin/sh
# -d : print date of latest git commit
# -h : print short hash of latest commit
# If we're lost in something that isn't a git repo or git is
# not installed, output these replacements:
# -d : current date as YYMMDD
# -h : NOGIT

if [ $# -eq 0 ] ; then
   printf "Info about the latest commit:\n\t-d\tDate\n\t-h\tHash value\n"
   exit 1
fi
case "$1" in
-d) git --version >/dev/null 2>&1 || { date "+%Y%m%d"; exit 0; }
    if git rev-parse --git-dir > /dev/null 2>&1; then
      git log --pretty=format:"%ai" | head -n1 | cut -c 1-4,6-7,9-10
    else
      date "+%Y%m%d"
    fi
    ;;

-h) git --version >/dev/null 2>&1 || { echo NOGIT; exit 0; }
    if git rev-parse --git-dir > /dev/null 2>&1; then
      git log --pretty=format:"%h" | head -n1 | awk '{print toupper($0)}'
    else
      echo NOGIT
    fi
    ;;
*) echo "Illegal option '$1'"
    ;;
esac
