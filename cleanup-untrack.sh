#!/usr/bin/env bash
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
for d in build-lsfg-termux build-rpath-test \
         phase3c-backup-20260820-184328 phase3c-restore-backup-20260820-184700 \
         gamenative-eden-vortek-xclipse package-vortek-xclipse zip-verify; do
  git rm -r --cached --ignore-unmatch -- "$d" >/dev/null 2>&1 || true
done
git ls-files | while IFS= read -r f; do
  case "$f" in
    */*) ;;
    *.so|*.log|*.zip|*.bak|*.bak-*)              git rm --cached --ignore-unmatch -- "$f" >/dev/null 2>&1 || true ;;
    android_vkprobe*.c|vkprobe.c|vk_sampler_phase2*.c|vk_image_*.c|vk_interpolation_*.c|vk_motion_*.c|vk_sampled_to_storage.c) git rm --cached --ignore-unmatch -- "$f" >/dev/null 2>&1 || true ;;
    lsfg_*.comp|lsfg_sampler_*.comp)             git rm --cached --ignore-unmatch -- "$f" >/dev/null 2>&1 || true ;;
    apply_phase*.py|rewrite_motion_oracle.py|fix_*.sh|run-motion-sweep.sh) git rm --cached --ignore-unmatch -- "$f" >/dev/null 2>&1 || true ;;
    xclipse940-*.txt|phase3b-*.txt|phase3c-*.txt) git rm --cached --ignore-unmatch -- "$f" >/dev/null 2>&1 || true ;;
    *'|'*)                                       git rm --cached --ignore-unmatch -- "$f" >/dev/null 2>&1 || true ;;
  esac
done
echo "Untrack complete. tracked now: $(git ls-files | wc -l)  deletions: $(git status --short | grep -c '^D ')"
