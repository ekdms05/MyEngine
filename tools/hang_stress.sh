#!/usr/bin/env bash
# hang_stress.sh — mye_tests.exe 종료-행(데드락) 스트레스 하네스.
# 사용법: hang_stress.sh <exe> <iterations> [timeout_secs]
# exit 124 (timeout)이면 행으로 집계. 통과 여부(133/133)도 함께 확인.
set -u
EXE="$1"
ITERS="${2:-50}"
TSECS="${3:-30}"

hangs=0
fails=0
badcount=0
for i in $(seq 1 "$ITERS"); do
    out="$(timeout "$TSECS" "$EXE" 2>&1)"
    code=$?
    if [ "$code" -eq 124 ]; then
        hangs=$((hangs+1))
        echo "  iter $i: HANG (exit 124)"
    elif [ "$code" -ne 0 ]; then
        fails=$((fails+1))
        echo "  iter $i: FAIL (exit $code)"
    fi
    # 통과 라인 확인
    if ! echo "$out" | grep -q "passed"; then
        badcount=$((badcount+1))
    fi
done
echo "=========================================="
echo "iterations=$ITERS hangs=$hangs nonzero_fails=$fails missing_pass_line=$badcount"
echo "=========================================="
exit $hangs
