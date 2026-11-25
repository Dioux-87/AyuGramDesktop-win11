#!/usr/bin/env python3
"""Rende prepare.py di AyuGram compatibile con i repository MSYS2 del 2026 e
con un runner GitHub non interattivo.

Eseguito dal workflow PRIMA della build (nella radice del repository).
Modifiche, tutte idempotenti:

1. `mingw-w64-x86_64-diffutils` non esiste piu' nei repo MSYS2 (2026): il
   pacchetto `diff` ora sta nel repo MSYS come `diffutils`.
2. `pacman -Syu` termina la shell che lo esegue (deve sostituire
   msys2-runtime): nella riga unica usata prima, i pacchetti successivi non
   venivano installati e prepare.py proseguiva senza errori. Ora l'aggiornamento
   del sistema sta da solo e il suo esito non blocca lo script; i pacchetti
   vengono installati in una seconda, unica invocazione.
3. prepare.py chiede conferma quando uno stage e' "stale" ("(r)ebuild ...
   (q)uit?"): su un runner non c'e' nessuno a rispondere. La scelta diventa
   automatica (ricostruisci).

NOTA: nessuna modifica a OpenSSL. Il perl necessario e' quello di MSYS2
(`mingw-w64-x86_64-perl`), gia' installato dallo stage 2, ed e' raggiungibile
perche' prepare.py mette `ThirdParty\\msys64\\mingw64\\bin` in PATH.
"""
import os
import re
import sys

PREPARE = os.path.join('Telegram', 'build', 'prepare', 'prepare.py')

if not os.path.exists(PREPARE):
    print('ERRORE: %s non trovato (esegui lo script nella radice del repository)'
          % PREPARE)
    sys.exit(1)

with open(PREPARE, encoding='utf-8') as f:
    original = f.read()
s = original
applied = []

# --- 1) diffutils: il pacchetto mingw64 non esiste piu' ----------------------
if '        mingw-w64-x86_64-diffutils ^\n' in s:
    s = s.replace('        mingw-w64-x86_64-diffutils ^\n', '')
    applied.append('rimosso mingw-w64-x86_64-diffutils')

# --- 2) pacman: aggiornamento separato dall'installazione --------------------
NEW_BLOCK = (
    '    SET MSYS2_PATH_TYPE=inherit\n'
    '    bash -c "pacman-key --init; pacman-key --populate"\n'
    '    bash -c "pacman -Syu --noconfirm" || echo [fix] aggiornamento MSYS2 interrotto, continuo\n'
    '    bash -c "pacman -S --noconfirm --needed diffutils make '
    'mingw-w64-x86_64-nasm mingw-w64-x86_64-gperf mingw-w64-x86_64-perl '
    'mingw-w64-x86_64-pkgconf" || bash -c "pacman -Syy --noconfirm --needed '
    'diffutils make mingw-w64-x86_64-nasm mingw-w64-x86_64-gperf '
    'mingw-w64-x86_64-perl mingw-w64-x86_64-pkgconf"\n'
)

patterns = [
    # versioni precedenti del fix: una sola riga con tutti i pacchetti
    re.compile(r'    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm[^"\n]*"\n'),
    # prepare.py originale di AyuGram
    re.compile(r'    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"\n'
               r'(?:    pacman -S --noconfirm --needed diffutils\n)?'),
]
for pat in patterns:
    if pat.search(s):
        s = pat.sub(NEW_BLOCK, s, count=1)
        applied.append('pacman: aggiornamento separato dai pacchetti')
        break

# 2b) rimuovi il vecchio blocco pacman multi-riga (farebbe un secondo -Syu)
old_multiline = re.compile(r'    pacman -Syu --noconfirm \^\n(?:        [^\n]*\n)+')
if old_multiline.search(s):
    s = old_multiline.sub('', s, count=1)
    applied.append('rimosso vecchio blocco pacman multi-riga')

# 2c) niente riga SET duplicata
while '    SET MSYS2_PATH_TYPE=inherit\n    SET MSYS2_PATH_TYPE=inherit\n' in s:
    s = s.replace('    SET MSYS2_PATH_TYPE=inherit\n    SET MSYS2_PATH_TYPE=inherit\n',
                  '    SET MSYS2_PATH_TYPE=inherit\n')

# --- 3) nessuna domanda interattiva ----------------------------------------
OLD_GETCH = """                print('(r)ebuild, rebuild (a)ll, (s)kip, (p)rint, (q)uit?: ', end='', flush=True)
                while True:
                    ch = 'r' if rebuildStale else getch()
"""
NEW_GETCH = """                print('CHANGED: ricostruisco automaticamente (nessun prompt)')
                while True:
                    ch = 'r'
"""
if OLD_GETCH in s:
    s = s.replace(OLD_GETCH, NEW_GETCH, 1)
    applied.append('niente prompt interattivo (ricostruzione automatica)')

# --- 4) se restano tracce di guardie "IF EXIST" aggiunte in passato, via ----
removed_guards = 0
for target in ('debug-VC-WIN64A /FS', 'VC-WIN64A /FS', 'debug-VC-WIN32 /FS',
               'VC-WIN32 /FS', 'debug-VC-WIN64-ARM /FS', 'VC-WIN64-ARM /FS /Gs4096'):
    guard = ('    IF EXIST %THIRDPARTY_DIR%\\perl-portable\\perl\\bin\\perl.exe '
             'SET PATH=%THIRDPARTY_DIR%\\perl-portable\\perl\\bin;%PATH%\n'
             '    perl Configure no-shared no-tests ' + target + '\n')
    plain = '    perl Configure no-shared no-tests ' + target + '\n'
    if guard in s:
        s = s.replace(guard, plain)
        removed_guards += 1
if removed_guards:
    applied.append('rimosse %d righe IF EXIST (non supportate in batch)' % removed_guards)

# --- scrittura -------------------------------------------------------------
if s == original:
    print("prepare.py gia' corretto: nessuna modifica necessaria.")
else:
    with open(PREPARE, 'w', encoding='utf-8', newline='\n') as f:
        f.write(s)
    print('Modifiche applicate: ' + ', '.join(applied))

# --- verifiche -------------------------------------------------------------
pacman_line = re.search(r'    bash -c "pacman -S --noconfirm --needed ([^"]+)"', s)
packages = pacman_line.group(1).split() if pacman_line else []
checks = {
    'niente pacchetto mingw64 diffutils': 'mingw-w64-x86_64-diffutils' not in s,
    'aggiornamento MSYS2 separato e non bloccante':
        re.search(r'bash -c "pacman -Syu --noconfirm" \|\| ', s) is not None,
    'pacchetti installati in una sola riga':
        all(p in packages for p in ('diffutils', 'make', 'mingw-w64-x86_64-nasm',
                                    'mingw-w64-x86_64-gperf', 'mingw-w64-x86_64-perl')),
    'nessun prompt interattivo': 'CHANGED: ricostruisco automaticamente' in s,
    'nessuna riga IF EXIST nel batch': 'IF EXIST %THIRDPARTY_DIR%' not in s,
}
for name, ok in checks.items():
    print(('OK   ' if ok else 'FAIL ') + name)
if not all(checks.values()):
    sys.exit(1)
print('prepare.py corretto.')
