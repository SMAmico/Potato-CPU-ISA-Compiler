#!/usr/bin/env bash
set -eu

usage() {
    printf 'Usage: %s [options] source.c [source.c ...]\n' "$0" >&2
    printf '\nOptions:\n' >&2
    printf '  -o, --output FILE       instruction output (default: first source .txt)\n' >&2
    printf '  --data-out FILE         data output\n' >&2
    printf '  --mif                   also write instruction and data MIF files\n' >&2
    printf '  --mif-out FILE          instruction MIF output\n' >&2
    printf '  --data-mif-out FILE     data MIF output\n' >&2
    printf '  --asm-out FILE          keep the merged assembly source\n' >&2
    printf '  --assembler FILE        EX-ISA assembler executable\n' >&2
}

if [ "$#" -eq 0 ]; then
    usage
    exit 1
fi

sources=()
assembler_args=()
output=
asm_output=
assembler=

while [ "$#" -gt 0 ]; do
    case "$1" in
        -o|--output)
            [ "$#" -ge 2 ] || { printf '%s requires a value\n' "$1" >&2; exit 1; }
            output=$2
            shift 2
            ;;
        --data-out|--mif-out|--data-mif-out)
            [ "$#" -ge 2 ] || { printf '%s requires a value\n' "$1" >&2; exit 1; }
            assembler_args+=("$1" "$2")
            shift 2
            ;;
        --mif)
            assembler_args+=("$1")
            shift
            ;;
        --asm-out)
            [ "$#" -ge 2 ] || { printf '%s requires a value\n' "$1" >&2; exit 1; }
            asm_output=$2
            shift 2
            ;;
        --assembler)
            [ "$#" -ge 2 ] || { printf '%s requires a value\n' "$1" >&2; exit 1; }
            assembler=$2
            shift 2
            ;;
        --)
            shift
            while [ "$#" -gt 0 ]; do
                sources+=("$1")
                shift
            done
            ;;
        -*)
            printf 'Unknown option: %s\n' "$1" >&2
            usage
            exit 1
            ;;
        *)
            sources+=("$1")
            shift
            ;;
    esac
done

if [ "${#sources[@]}" -eq 0 ]; then
    printf 'At least one C source is required\n' >&2
    usage
    exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for i in "${!sources[@]}"; do
    sources[$i]=$(realpath "${sources[$i]}")
    [ -f "${sources[$i]}" ] || { printf 'Input file not found: %s\n' "${sources[$i]}" >&2; exit 1; }
done

if [ -z "$output" ]; then
    output=${sources[0]%.c}.txt
fi
output=$(realpath -m "$output")

if [ -z "$assembler" ]; then
    assembler=${ASSEMBLER_EX_ISA:-}
fi
if [ -z "$assembler" ]; then
    for candidate in \
        "$project_dir/assembler-EX_ISA.exe" \
        "$project_dir/assembler-EX_ISA"; do
        if [ -x "$candidate" ]; then
            assembler=$candidate
            break
        fi
    done
fi
if [ -z "$assembler" ]; then
    printf 'EX-ISA assembler not found; use --assembler FILE or ASSEMBLER_EX_ISA\n' >&2
    exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/8cc-ex-isa.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

if [ -x "$project_dir/8cc.exe" ]; then
    compiler=$project_dir/8cc.exe
elif [ -x "$project_dir/8cc" ]; then
    compiler=$project_dir/8cc
else
    printf '8cc compiler not found in project root; move the working executable from build/ first\n' >&2
    exit 1
fi

merged=$tmpdir/merged.s
: > "$merged"
for i in "${!sources[@]}"; do
    source=${sources[$i]}
    stem=$(basename "$source")
    stem=${stem%.c}
    stem=$(printf '%s' "$stem" | sed 's/[^A-Za-z0-9_]/_/g')
    module_id="m${i}_${stem}"
    asm=$tmpdir/module-${i}.s
    source_dir=$(dirname "$source")
    printf '# EX-ISA module %s: %s\n' "$module_id" "$source" >> "$merged"
    "$compiler" -I"$source_dir" -I"$project_dir/include" -mex-isa \
        -S --module-id "$module_id" -o "$asm" "$source"
    cat "$asm" >> "$merged"
    printf '\n# End EX-ISA module %s\n' "$module_id" >> "$merged"
done

if [ -n "$asm_output" ]; then
    asm_output=$(realpath -m "$asm_output")
    cp "$merged" "$asm_output"
fi

"$assembler" "$merged" "$output" "${assembler_args[@]}"
printf 'Linked %d EX-ISA source module(s) into %s\n' "${#sources[@]}" "$output"