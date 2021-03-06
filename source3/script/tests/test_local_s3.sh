#!/bin/sh

# this runs the file serving tests that are expected to pass with samba3

if [ $# != 0 ]; then
	cat <<EOF
Usage: test_local_s3.sh
EOF
	exit 1
fi

incdir=$(dirname $0)/../../../testprogs/blackbox
. $incdir/subunit.sh

failed=0

cd $SELFTEST_TMPDIR || exit 1

if test -x $BINDIR/talloctort; then
	testit "talloctort" $VALGRIND $BINDIR/talloctort ||
		failed=$(expr $failed + 1)
else
	echo "Skipping talloctort"
fi

testit "replace_testsuite" $VALGRIND $BINDIR/replace_testsuite ||
	failed=$(expr $failed + 1)

if test -x $BINDIR/tdbtorture; then
	testit "tdbtorture" $VALGRIND $BINDIR/tdbtorture ||
		failed=$(expr $failed + 1)
else
	echo "Skipping tdbtorture"
fi

testit "smbconftort" $VALGRIND $BINDIR/smbconftort $CONFIGURATION ||
	failed=$(expr $failed + 1)

testok $0 $failed
