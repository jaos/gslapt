#!/bin/bash
set -efu

INPUT=$1
OUTPUT=$2
SRCDIR=$3
TMPDIR=$(mktemp -d)
WORKINGDIR=${PWD}
grep '=' "${INPUT}" | sed "s/_\(.*\)=\(.*\)/\1=\"\2\"/" > "${TMPDIR}"/series_map.rc_quoted
sed "s/\_//" "${INPUT}" > "${OUTPUT}"

mkdir -p "${TMPDIR}"/po
pushd "${TMPDIR}"/po
intltool-merge --quoted-style -m -u "${SRCDIR}"/po "${TMPDIR}"/series_map.rc_quoted "${OUTPUT}"
for i in $(find . -mindepth 1 -maxdepth 1 -type d -exec basename {} \;);do
    sed -i "s/\(.*\)=\(.*\)/\1[$i]=\2/" $i/series_map.rc
    sed "s/\"//g" $i/series_map.rc >> "${WORKINGDIR}/${OUTPUT}"
done
popd
rm -rf "${TMPDIR}"
sed -i -re 's,XXXDOTXXX,.,g' "${OUTPUT}"
sed -i -re 's,XXXSLASHXXX,/,g' "${OUTPUT}"
