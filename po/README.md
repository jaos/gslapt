Translations live in Transifex (`jaos/slapt-get`, resource `gslapt`)

- regen the template: `meson compile -C build gslapt-pot`
- regen the po files: `meson compile -C build gslapt-update-po`
- check: `while read -r po;do msgfmt --check po/${po}.po;done < po/LINGUAS`
- pull via tx: `tx pull --all --force --translations --mode translator`, optionally specific languages with `--languages <comma delimited langs>`
- push: `tx push --source --translation --force`, optionally specific langauges with `--languages <comma delimited langs>`
