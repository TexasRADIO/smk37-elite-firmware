#!/bin/sh
set -eu

if git ls-files | grep -E -i '(recovery|handover|research|evidence|audit|temporary|tmp|\.pyc$|test)'; then
  printf '%s\n' 'Disallowed public release path.' >&2
  exit 1
fi

PYTHONDONTWRITEBYTECODE=1 python3 scripts/verify-package-identities.py
