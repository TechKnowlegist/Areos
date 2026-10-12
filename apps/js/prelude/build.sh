#!/bin/sh
# Join the prelude files into one function: prelude.js (for the host to run).
cd "$(dirname "$0")"
{
  echo "(function (N) { 'use strict';"
  for f in 00_base 10_node 12_html 20_select 30_parse 40_url 50_doc 60_web 62_intl 70_page 90_boot; do
    cat $f.js; echo
  done
  echo "})"
} > "${1:-prelude.js}"
