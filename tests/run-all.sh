#!/bin/sh
# Run every suite under tests/ and summarise.
#
# The suites fall into two groups. Six score the emulator against console
# captures and need a checkout to compare against:
#
#   PS2AUTOTESTS=/path/to/ps2autotests   (ee, fpu, iop, mmi, vif, vu)
#   PS1TESTS=/path/to/ps1-tests          (iop: the GTE and MDEC oracles)
#
#     git clone https://github.com/unknownbrackets/ps2autotests
#     git clone https://github.com/JaCzekanski/ps1-tests
#
# Without those the capture-backed suites skip rather than fail, so this
# is still worth running with neither set -- the rest do not need them.
#
# SANITIZER is passed through, so
#
#   SANITIZER=undefined sh tests/run-all.sh
#
# instruments sanitizer-aware suites with UBSan. Fixed harnesses keep their
# own instrumentation. Clean between sanitizers rather than reusing objects.
#
# Exits non-zero if any suite does, and says which.
set -u

# Both conventions exist in the suites; keep their instrumentation in sync.
SANITIZER=${SANITIZER:-}
SANFLAGS=${SANFLAGS:-}
if [ -n "$SANITIZER" ] && [ -z "$SANFLAGS" ]; then
	SANFLAGS="-fsanitize=$SANITIZER -fno-omit-frame-pointer"
fi
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1"
export SANITIZER SANFLAGS UBSAN_OPTIONS

DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# Every directory with a build.sh, found rather than listed. A suite added
# to a hardcoded list by hand is a suite that silently stops running when
# someone forgets, and four had already stopped: gs, settings, cdvdread
# and mtvuring were all in the tree and none of them were in the list.
SUITES=$(for d in "$DIR"/*/build.sh; do
	[ -f "$d" ] || continue
	b=${d%/build.sh}
	printf '%s ' "${b##*/}"
done)

# Fixed sanitizer builds and release-only frame checks do not vary by lane.
# Keep all repetitions once in CI; standalone runs still execute every suite.
covered_in_plain_lane() {
	case "$1" in
		faultstress|spsc|mtvuring|pgscache|stackframe)
			[ "${GITHUB_ACTIONS:-}" = true ] && [ -n "$SANITIZER" ] ;;
		*) return 1 ;;
	esac
}

# Rough seconds per suite (measured on a 7-core workstation, plain lane), so shards come out even. A suite
# missing here counts as WEIGHT_DEFAULT: new suites are still discovered and
# still run exactly once, only less well balanced until listed.
WEIGHT_DEFAULT=2
suite_weight() {
	case "$1" in
		smaprx) echo 135 ;;
		spsc) echo 134 ;;
		pgs) echo 120 ;;
		stackframe) echo 90 ;;
		clinkage) echo 40 ;;
		cheaders) echo 28 ;;
		spu2) echo 23 ;;
		gsvec) echo 20 ;;
		gslm) echo 18 ;;
		gsv) echo 16 ;;
		ata) echo 14 ;;
		cdvdread) echo 14 ;;
		patch) echo 13 ;;
		gssearch) echo 11 ;;
		eestage) echo 9 ;;
		fpaudit) echo 9 ;;
		ipu) echo 9 ;;
		usbstate) echo 8 ;;
		vkpipekey) echo 8 ;;
		gsupscale) echo 7 ;;
		mtgsown) echo 7 ;;
		d3d12warm) echo 6 ;;
		gs) echo 4 ;;
		settings) echo 4 ;;
		ee) echo 3 ;;
		gsgroup) echo 3 ;;
		*) echo "$WEIGHT_DEFAULT" ;;
	esac
}

# SHARD=i SHARDS=n runs the i-th of n disjoint subsets of the suites (i from
# 0), chosen greedily so the heaviest suites spread out. The subsets depend
# only on the suite list and weights, so every shard computes the same split
# and together they run each suite exactly once. Unset, one shard runs all.
SHARDS=${SHARDS:-1}
SHARD=${SHARD:-0}
case "$SHARDS$SHARD" in
	*[!0-9]*|'') echo "SHARDS and SHARD must be non-negative integers" >&2; exit 2 ;;
esac
if [ "$SHARDS" -lt 1 ] || [ "$SHARD" -ge "$SHARDS" ]; then
	echo "need 0 <= SHARD < SHARDS, got SHARD=$SHARD SHARDS=$SHARDS" >&2
	exit 2
fi

# Suites to run in this lane, skipping those another lane already covered.
eligible=""
covered=""
for s in $SUITES; do
	if covered_in_plain_lane "$s"; then
		covered="$covered $s"
	else
		eligible="$eligible $s"
	fi
done

if [ "$SHARDS" -eq 1 ]; then
	mine=$eligible
else
	mine=$(for s in $eligible; do printf '%s %s\n' "$(suite_weight "$s")" "$s"; done \
		| sort -k1,1nr -k2,2 \
		| awk -v shard="$SHARD" -v shards="$SHARDS" '
			{ best = 0
			  for (i = 1; i < shards; i++) if (load[i] < load[best]) best = i
			  load[best] += $1
			  if (best == shard) printf "%s ", $2 }')
fi

# LIST=1 prints this shard's suites and runs nothing.
if [ "${LIST:-}" = 1 ]; then
	for s in $mine; do printf '%s\n' "$s"; done
	exit 0
fi

failed=""
ran=""

# One shard reports the skips, so the log says so once per lane.
if [ "$SHARD" -eq 0 ]; then
	for s in $covered; do printf 'covered in plain lane: %s\n' "$s"; done
fi
[ "$SHARDS" -gt 1 ] && printf 'shard %s of %s: %s\n' "$((SHARD + 1))" "$SHARDS" "${mine% }"

for s in $mine; do
	printf '\n======== %s ========\n' "$s"
	if sh "$DIR/$s/build.sh"; then
		ran="$ran $s"
	else
		failed="$failed $s"
	fi
done

printf '\n======== summary ========\n'
[ -n "$ran" ]     && printf 'passed: %s\n' "${ran# }"

if [ -n "$failed" ]; then
	printf 'FAILED: %s\n' "${failed# }"
	exit 1
fi

if [ -z "${PS2AUTOTESTS:-}" ]; then
	printf '\nPS2AUTOTESTS was not set, so the console-scored suites only\n'
	printf 'reported the checks that need no capture. Set it for the rest.\n'
fi
exit 0
