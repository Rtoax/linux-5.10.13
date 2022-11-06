#!/bin/bash

set -e

SRC_LINUX=/home/rongtao/Git/linux
DST=$PWD

copy_file()
{
	local _f=$1

	cd $SRC_LINUX
	for i in $(find -name $_f)
	do
		# Skip exist
		test -e $DST/$i && echo "EXIST $i" && continue
		# Copyright if not exist
		cp $i $DST/$i || true
	done
	cd -
}

copy_file Kconfig
copy_file Kbuild
copy_file Makefile

