#!/usr/bin/env bash
#
# Copyright (C) 2006 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
SELF=${0##*/}

[ -z "$STRIP" ] && {
  echo "$SELF: strip command not defined (STRIP variable not set)"
  exit 1
}

for F in "$@"; do
  while [ -h "$F" ]; do
    F=$(readlink "$F")
  done

  case "$(file -b "$F")" in
    *ELF\ *executable\|relocatable\|shared\ object*)
      echo "$SELF: $F"
      [ "${F##*.}" == "o" ] || {
        [ "${F##*.}" == "ko" ] && eval "$STRIP_KMOD $F" || eval "$STRIP $F"
      }
      ;;
  esac
done
