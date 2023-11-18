#! /bin/bash
set -e

python3 -m pylint -d C0103,C0209 *.py
