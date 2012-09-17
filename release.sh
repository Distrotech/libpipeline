#! /bin/sh
set -e

# Build automatically generated files
./autogen.sh

# Basic configure to get 'make distcheck'
./configure

make distcheck
