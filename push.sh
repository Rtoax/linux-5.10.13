#!/bin/bash 
# 

if [ $# -lt 1 ]; then
	echo "need commit string"
	exit 1
fi

git add --all
git status
git commit -m "$*"
if [ $? = 0 ]; then
	git push
fi
