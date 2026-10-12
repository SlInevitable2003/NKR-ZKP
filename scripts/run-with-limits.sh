#!/usr/bin/env bash
set -euo pipefail

memory_mib=4096
seconds=300

while [[ $# -gt 0 ]]; do
  case "$1" in
    --memory-mib)
      memory_mib=$2
      shift 2
      ;;
    --timeout)
      seconds=$2
      shift 2
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "usage: $0 [--memory-mib MiB] [--timeout seconds] -- command..." >&2
      exit 2
      ;;
  esac
done

if [[ $# -eq 0 || ! "$memory_mib" =~ ^[0-9]+$ || ! "$seconds" =~ ^[1-9][0-9]*$ ]]; then
  echo "invalid resource limit or missing command" >&2
  exit 2
fi

available_mib=$(awk '/MemAvailable:/ { print int($2 / 1024) }' /proc/meminfo)
if (( available_mib < 2048 )); then
  echo "refusing to start: only ${available_mib} MiB host memory is available" >&2
  exit 75
fi

if (( memory_mib > 0 && memory_mib > available_mib - 512 )); then
  memory_mib=$((available_mib - 512))
fi

if (( memory_mib > 0 )); then
  echo "resource guard: memory=${memory_mib} MiB timeout=${seconds}s"
  ulimit -Sv $((memory_mib * 1024))
else
  # CUDA/HIP drivers reserve a very large virtual address range. A finite
  # RLIMIT_AS makes even tiny device allocations fail, so GPU runs use input
  # size checks plus device-side free-memory checks instead.
  echo "resource guard: memory=driver-managed timeout=${seconds}s"
fi
exec timeout --signal=TERM --kill-after=10 "$seconds" "$@"
