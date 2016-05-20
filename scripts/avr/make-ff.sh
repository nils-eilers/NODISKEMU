#/bin/sh
rm -f ff.bin
i=1
#EESIZE=4096
EESIZE=$1
while [ $i -le $EESIZE ]; do
	printf "\377" >> ff.bin
	i=$((i+1))
done
