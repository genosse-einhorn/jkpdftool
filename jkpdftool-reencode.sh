#!/bin/sh

if [ "$1" = "-?" ] || [ "$1" = "--help" ]; then
    (
    printf 'Usage:\n'
    printf '  %s <INPUT-PDF >OUTPUT-PDF\n' "$(basename "$0")"
    printf '\n'
    printf 'Recreate the PDF file using GhostScript. This can be a viable workaround\n'
    printf 'when the other tools won’t process your PDF file correctly (which is considered\n'
    printf 'a bug, but poppler isn’t always that fast in fixing them and maybe you don’t\n'
    printf 'want to send them your PDF file but just want to get on with your life)\n'
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

exec gs -sDEVICE=pdfwrite "$@" -q -o - -
