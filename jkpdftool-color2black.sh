#!/bin/sh

if [ "$1" = "-?" ] || [ "$1" = "--help" ]; then
    (
    printf 'Usage:\n'
    printf '  %s <INPUT-PDF >OUTPUT-PDF\n' "$(basename "$0")"
    printf '\n'
    printf 'Convert a PDF to all black-and-white via GhostScript\n\n'
    ) 1>&2
    exit 1
fi

if [ -t 0 ]; then
    printf 'ERROR: refusing to read PDF from TTY\n' 1>&2
    exit 1
fi

if [ -t 1 ]; then
    printf 'ERROR: refusing to write PDF to TTY\n' 1>&2
    exit 1
fi

if ! command -v gs >/dev/null 2>&1; then
    printf 'ERROR: need ‘gs’ command line utility, please install GhostScript.\n' 1>&2
    exit 1
fi

exec gs -sDEVICE=pdfwrite -dBlackText -dBlackVector "$@" -q -o - -

