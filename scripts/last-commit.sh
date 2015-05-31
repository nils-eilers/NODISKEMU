#!/bin/sh
if [ $# -eq 0 ] ; then
   printf "Info about the latest commit:\n\t-d\tDate\n\t-h\tHash value\n"
   exit 1
fi
case "$1" in
-d) git log --pretty=format:"%ai" | head -n1 | cut -c 1-4,6-7,9-10
    ;;
-h) git log --pretty=format:"%h" | head -n1 | awk '{print toupper($0)}'
    ;;
*) echo "Illegal option '$1'"
    ;;
esac
