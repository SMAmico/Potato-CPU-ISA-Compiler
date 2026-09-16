#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    printf 'Usage: %s input.c [output.s]\n' "$0" >&2
    exit 1
fi

input=$1
if [ ! -f "$input" ]; then
    printf 'Input file not found: %s\n' "$input" >&2
    exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
input=$(realpath "$input")
input_dir=$(dirname "$input")
compiler_input=$(realpath --relative-to="$project_dir" "$input")
compiler_include=$(realpath --relative-to="$project_dir" "$input_dir")
output=${2:-${input%.c}.s}
output=$(realpath -m "$output")

cd "$project_dir"
make -C "$project_dir" TARGET=ex-isa

if [ -x "$project_dir/8cc.exe" ]; then
    compiler="$project_dir/8cc.exe"
else
    compiler="$project_dir/8cc"
fi

"$compiler" -I"$compiler_include" -mex-isa -S -o "$output" "$compiler_input"
printf 'Generated EX-ISA assembly: %s\n' "$output"