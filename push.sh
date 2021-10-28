#!/bin/bash 
# 

git add --all
git status
git commit -m $*
if [ $? = 0 ]; then
	git push
fi
