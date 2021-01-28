#!/bin/sh

SBINDIR="${1}"
OUT="${2}"
(env echo -e "#!/bin/sh\npkexec --disable-internal-agent ${SBINDIR}/gslapt \$@") > "${OUT}"
chmod +x "${OUT}"


