#!/bin/sh

aclocal
intltoolize --copy --force
autoreconf --install --force
