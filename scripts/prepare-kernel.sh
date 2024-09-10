#!/bin/sh
set -e
set -u

unset CDPATH

patch_copytempfile() {
	file="$1"
	if ! test -f "$temp_tree/$file"; then
		subdir=`dirname "$file"`
		mkdir -p "$temp_tree/$subdir"
		cp "$linux_tree/$file" "$temp_tree/$file"
	fi
}

patch_append() {
	file="$1"
	if test "x$output_patch" = "x"; then
		if test -L "$linux_tree/$file" ; then
			mv "$linux_tree/$file" "$linux_tree/$file.orig"
			cp "$linux_tree/$file.orig" "$linux_tree/$file"
		else
			cp "$linux_tree/$file" "$linux_tree/$file.orig"
		fi
		chmod +w "$linux_tree/$file"
		cat >> "$linux_tree/$file"
	else
		patch_copytempfile "$file"
		cat >> "$temp_tree/$file"
	fi
}

patch_reverse() {
	file="$1"
	if test "x$output_patch" = "x"; then
		if test -L "$linux_tree/$file" ; then
			rm "$linux_tree/$file"
		fi
		mv "$linux_tree/$file.orig" "$linux_tree/$file"
	fi
}

patch_link() {
	recursive="$1"		   # "r" or "n"
	link_file="$2"		   # "m", "n" or some file (basename) from $target_dir
	target_dir="$3"
	link_dir="$4"

	(
	if test \! \( x$link_file = xm -o x$link_file = xn \); then
		find_clean_opt="-name $link_file"
		find_link_opt=$find_clean_opt
	else
		link_makefiles_opt=""
		if test x$link_file = xm; then
			link_makefiles_opt="-name Makefile -o"
		fi
		if test x$recursive = xr; then
			recursive_opt="-mindepth 1"
		else
			recursive_opt="-maxdepth 1"
		fi
		find_clean_opt="$recursive_opt \( $link_makefiles_opt -name Kconfig -o -name '*.[chS]' -o -name '*.sh' \)"
		find_link_opt="$recursive_opt \( $link_makefiles_opt -name Kconfig -o -name '*.[chS]' -o -name '*.sh' \)"
	fi

	if test "x$output_patch" = "x" -a "x$reverse" = "x0" -a -e $linux_tree/$link_dir; then
		cd $linux_tree/$link_dir && eval find . $find_clean_opt | while read f; do
			if test -L $f -a ! -e $f; then rm -f $f; fi
		done
	fi

	cd $xenomai_root/$target_dir &&
	eval find . $find_link_opt |
	while read f; do
		f=`echo $f | cut -d/ -f2-`
		d=`dirname $f`
		if test "x$output_patch" = "x"; then
			if test x$reverse = x1; then
				rm -f $linux_tree/$link_dir/$f
			else
				mkdir -p $linux_tree/$link_dir/$d
				if test ! $xenomai_root/$target_dir/$f -ef $linux_tree/$link_dir/$f; then
					ln -sf $xenomai_root/$target_dir/$f $linux_tree/$link_dir/$f
				fi
			fi
		else
			if test -e $linux_tree/$link_dir/$f; then
				echo "$me: warning: $link_dir/$f already present in Linux kernel tree, output patch might be defective" >&2
			fi
			mkdir -p $temp_tree/$link_dir/$d
			cp $xenomai_root/$target_dir/$f $temp_tree/$link_dir/$f
		fi
	done
	if test x$reverse = x1; then
		if ! find $linux_tree/$link_dir -not -type d | grep -q . ; then
			# shellcheck disable=SC2115
			rm -rf ${linux_tree}/$link_dir
		fi
	fi
	)
}

generate_patch() {
	(
	cd "$temp_tree"
	find . -type f |
	while read f; do
		diff -Naurd "$linux_tree/$f" "$f" |
		sed -e "s,^--- ${linux_tree}/\.\(/.*\)$,--- linux\1," \
			-e "s,^+++ \.\(/.*\)$,+++ linux-patched\1,"
	done
	)
}

usage='usage: prepare-kernel --linux=<linux-tree> [--dovetail=<dovetail-patch>] [--arch=<arch>] [--outpatch=<file> [--verbose] [--reverse]'
me=`basename $0`

# Default path to kernel tree
linux_tree=.
output_patch=""
linux_arch=""
reverse=0
pipeline_patch=""
verbose=0

while test $# -gt 0; do
	case "$1" in
	--linux=*)
		linux_tree=`echo $1|sed -e 's,^--linux=\\(.*\\)$,\\1,g'`
		linux_tree=`eval "echo $linux_tree"`
		;;
	--dovetail=*)
		pipeline_patch=`echo $1|sed -e 's,^--dovetail=\\(.*\\)$,\\1,g'`
		pipeline_patch=`eval "echo $pipeline_patch"`
		;;
	--arch=*)
		linux_arch=`echo $1|sed -e 's,^--arch=\\(.*\\)$,\\1,g'`
		;;
	--outpatch=*)
		output_patch=`echo $1|sed -e 's,^--outpatch=\\(.*\\)$,\\1,g'`
		;;
	--filterkvers=*)
		echo "$me: warning: --filterkvers= is deprecated and now a no-op" >&2
		;;
	--filterarch=*)
		echo "$me: warning: --filterarch= is deprecated and now a no-op" >&2
		;;
	--forcelink)
		echo "$me: warning: --forcelink is deprecated and now a no-op" >&2
		;;
	--default)
		echo "$me: warning: --default is deprecated and now always on" >&2
		;;
	--verbose)
		verbose=1
		;;
	--reverse)
		reverse=1
		;;
	--help)
		echo "$usage"
		exit 0
		;;
	*)
		echo "$me: unknown flag: $1" >&2
		echo "$usage" >&2
		exit 1
		;;
	esac
	shift
done

# Infere the location of the Xenomai source tree from
# the path of the current script.

script_path=`realpath $0`
xenomai_root=`dirname $script_path`/..
xenomai_root=`cd $xenomai_root && pwd`

# Check the Linux tree

default_linux_tree=/lib/modules/`uname -r`/source

if test x$linux_tree = x; then
	linux_tree=$default_linux_tree
fi

linux_tree=`cd $linux_tree && pwd`

if test "$(head -1 $linux_tree/README 2> /dev/null)" != "Linux kernel"; then
	echo "$me: $linux_tree is not a valid Linux kernel tree" >&2
	exit 2
fi

# Create an empty output patch file, and initialize the temporary tree.
if test "x$output_patch" != "x"; then
	temp_tree=`mktemp -d prepare-kernel-XXX --tmpdir`
	if [ $? -ne 0 ]; then
		echo Temporary directory could not be created.
		exit 1
	fi

	if test x$reverse = x1; then
		echo "$me: --reverse and --outpatch are not compatible"
		exit 1
	fi

	patchdir=`dirname $output_patch`
	patchdir=`cd $patchdir && pwd`
	output_patch=$patchdir/`basename $output_patch`
	echo > "$output_patch"

fi

if test x$linux_arch = x; then
	target_linux_archs="x86 arm arm64"
else
	case "$linux_arch" in
		x86*|amd*)
		target_linux_archs="x86"
		;;
	arm)
		target_linux_archs="arm"
		;;
	arm64|aarch64)
		target_linux_archs="arm64"
		;;
	*)
		echo "$me: unsupported architecture: $linux_arch" >&2
		exit 1
		;;
	esac
fi

for a in $target_linux_archs; do
	if test x$reverse = x1; then
		if ! grep -q CONFIG_XENOMAI $linux_tree/arch/$a/Makefile; then
			echo "$me: $linux_tree is not prepared with Xenomai" >&2
			exit 2
		fi
	fi
done

foo=`grep '^KERNELSRC	 := ' $linux_tree/Makefile | cut -d= -f2`
if [ ! -z $foo ] ; then
	linux_tree=$foo
fi
unset foo

if test x$verbose = x1; then
	echo "Preparing kernel $linux_tree..."
fi

if test -r $linux_tree/include/linux/dovetail.h; then
	if test x$verbose = x1; then
		echo "IRQ pipeline found - bypassing patch."
	fi
else
	if test x$verbose = x1; then
		echo "$me: no IRQ pipeline support found." >&2
	fi
	if test x$pipeline_patch = x; then
		echo "$me: no IRQ pipeline support found in $linux_tree and --dovetail= not set" >&2
		exit 2
	fi
	patchdir=`dirname $pipeline_patch`;
	patchdir=`cd $patchdir && pwd`
	pipeline_patch=$patchdir/`basename $pipeline_patch`
	curdir=$PWD
	cd $linux_tree && patch --dry-run -p1 -f < $pipeline_patch || {
		cd $curdir;
		echo "$me: Unable to patch kernel $linux_tree with `basename $pipeline_patch`." >&2
		exit 2;
	}
	patch -p1 -f -s < $pipeline_patch
	cd $curdir
fi

for a in $target_linux_archs; do
	if test \! -r $linux_tree/arch/$a/include/asm/dovetail.h; then
		echo "$me: $linux_tree has no IRQ pipeline support for $a" >&2
		exit 2
	fi
done

if test x$verbose = x1; then
	echo "IRQ pipeline installed."
fi

if ! grep -q XENOMAI $linux_tree/init/Kconfig; then
	version_stamp=`cat $xenomai_root/config/version-code`
	version_major=`expr $version_stamp : '\([[0-9]]*\)' || true`
	version_minor=`expr $version_stamp : '[[0-9]]*\.\([[0-9]*]*\)' || true`
	revision_level=`expr $version_stamp : '[[0-9]]*\.[[0-9]*]*\.\([[0-9]*]*\)' || true`
if [ -z "$revision_level" ]; then
	revision_level=0
fi
version_string=`cat $xenomai_root/config/version-label`
sed -e "s,@VERSION_MAJOR@,$version_major,g" \
	-e "s,@VERSION_MINOR@,$version_minor,g" \
	-e "s,@REVISION_LEVEL@,$revision_level,g" \
	-e "s,@VERSION_STRING@,$version_string,g" \
	$xenomai_root/scripts/Kconfig.frag |
		patch_append init/Kconfig
elif test x$reverse = x1; then
	patch_reverse init/Kconfig
fi

for a in $target_linux_archs; do
	if ! grep -q CONFIG_XENOMAI $linux_tree/arch/$a/Makefile; then
		p1="KBUILD_CFLAGS += -I\$(srctree)/arch/\$(SRCARCH)/xenomai/include -I\$(srctree)/include/xenomai"
		p2="core-\$(CONFIG_XENOMAI)	+= arch/$a/xenomai/"
		(echo; echo $p1; echo $p2) | patch_append arch/$a/Makefile
	elif test x$reverse = x1; then
		patch_reverse arch/$a/Makefile
	fi
done

if ! grep -q CONFIG_XENOMAI $linux_tree/drivers/Makefile; then
	p="obj-\$(CONFIG_XENOMAI)		+= xenomai/"
	( echo ; echo $p ) | patch_append drivers/Makefile
elif test x$reverse = x1; then
	patch_reverse drivers/Makefile
fi

if ! grep -q CONFIG_XENOMAI $linux_tree/kernel/Makefile; then
	p="obj-\$(CONFIG_XENOMAI)		+= xenomai/"
	( echo ; echo $p ) | patch_append kernel/Makefile
elif test x$reverse = x1; then
	patch_reverse kernel/Makefile
fi

# Create local directories then symlink to the source files from
# there, so that we don't pollute the Xenomai source tree with
# compilation files.
#
# Keep link dirs (4th parameter) descending, otherwise --reverse
# is not able to cleanup these directories!

for a in $target_linux_archs; do
	patch_link r m kernel/cobalt/arch/$a arch/$a/xenomai
	patch_link n n kernel/cobalt/include/dovetail arch/$a/include/dovetail
done
patch_link n cobalt-core.h kernel/cobalt/trace include/trace/events
patch_link n cobalt-rtdm.h kernel/cobalt/trace include/trace/events
patch_link n cobalt-posix.h kernel/cobalt/trace include/trace/events
patch_link r n kernel/cobalt/include/asm-generic/xenomai include/asm-generic/xenomai
patch_link n m kernel/cobalt/posix kernel/xenomai/posix
patch_link n m kernel/cobalt/rtdm kernel/xenomai/rtdm
patch_link n m kernel/cobalt/dovetail kernel/xenomai/pipeline
patch_link n m kernel/cobalt kernel/xenomai
patch_link r m kernel/drivers drivers/xenomai
patch_link n n include/cobalt/kernel include/xenomai/cobalt/kernel
patch_link r n include/cobalt/kernel/dovetail/pipeline include/xenomai/pipeline
patch_link r n include/cobalt/uapi include/xenomai/cobalt/uapi
patch_link r n include/rtdm/uapi include/xenomai/rtdm/uapi
patch_link r n include/cobalt/kernel/rtdm include/xenomai/rtdm
patch_link n stdarg.h kernel/cobalt/include/linux include/xenomai/linux
patch_link n version.h include/xenomai include/xenomai

if test "x$output_patch" != "x"; then
	if test x$verbose = x1; then
		echo 'Generating patch.'
	fi
	generate_patch > "$output_patch"
	rm -rf $temp_tree
fi

if test x$verbose = x1; then
	echo 'Links installed.'
	echo 'Build system ready.'
fi

exit 0
