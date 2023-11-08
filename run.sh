#!/usr/bin/env bash

make thread
	

list=()
for file in tests/* 
do
	test=`basename "$file" .c`

	make test name="$test"
	list+=("$test")
done

for i in "${list[@]}"
do
	echo ""
	echo "----- RUNNING $i"
	./"$i" &> tmp
	if [ $? -eq 139 ]; then
		echo "SEGFAULT"
	fi

	echo "##### OUTPUT:"
	cat tmp
	
	echo "---"
	echo "***** DIFF BETWEEN EXPECTED:"
	diff tmp output/"$i".out
	
	echo ""
done
rm tmp

for i in "${list[@]}"
do
	
	make clean_test name="$i"

done