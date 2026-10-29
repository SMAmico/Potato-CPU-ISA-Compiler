#!/usr/bin/env bash
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/8cc-entry-test.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

wrapper=$project_dir/compile-ex-isa.sh
main_source=$project_dir/test/ex-isa-tests/ex_isa_link_main.c
provider_source=$project_dir/test/ex-isa-tests/ex_isa_link_provider.c

"$wrapper" --mif --asm-out "$tmpdir/main.s" -o "$tmpdir/main.txt" \
    "$provider_source" "$main_source" >"$tmpdir/main.stdout" 2>"$tmpdir/main.stderr"
grep -Fq '00 : 6E00; -- jmp main' "$tmpdir/main.mif"
grep -Fq '02 : 6E26; -- jmp main' "$tmpdir/main.mif"
grep -Fq '26 : 8000; -- nop' "$tmpdir/main.mif"

if "$wrapper" --mif --asm-out "$tmpdir/no-main.s" -o "$tmpdir/no-main.txt" \
    "$provider_source" >"$tmpdir/no-main.stdout" 2>"$tmpdir/no-main.stderr"; then
    printf 'Expected the no-main build to exit nonzero\n' >&2
    exit 1
else
    status=$?
fi

[ "$status" -eq 1 ]
grep -Fxq "undefined reference to 'main'" "$tmpdir/no-main.stderr"
grep -Fq '00 : 5000; -- hlt' "$tmpdir/no-main.mif"
grep -Fq '01 : 8000; -- nop' "$tmpdir/no-main.mif"
grep -Fq 'external_function:' "$tmpdir/no-main.s"

printf 'EX-ISA entry wrapper tests passed\n'