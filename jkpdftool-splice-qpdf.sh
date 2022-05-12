#!/bin/sh

# TODO: help

exec qpdf --pages "$@" -- --empty -
