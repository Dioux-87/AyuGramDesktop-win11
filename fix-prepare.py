#!/usr/bin/env python3
"""Rende preparabile AyuGram con i repository MSYS2/MSYS attuali (2026).

Eseguito dal workflow PRIMA della build: modifica Telegram/build/prepare/prepare.py
con sostituzioni chirurgiche e verifica che siano state applicate.

1. `mingw-w64-x86_64-diffutils` non esiste piu' nei repo MSYS2: il pacchetto
   `diff` ora sta nel repo MSYS come `diffutils`.
2. Strawberry Perl portable: garantisce un perl.exe funzionante anche se
   `mingw-w64-x86_64-perl` non fosse disponibile.
3. MSYS2 "shell diversa": dopo `pacman -Syu` la shell viene terminata e
   riavviata di nascosto; le righe successive dello stesso script .bat possono
   non essere piu' eseguite, pur senza errore. Quindi si installa TUTTO in una
   singola riga (un solo `bash -c`).
4. OpenSSL (stage 12) ha bisogno di `nasm` (lo cerca con `nasm -v` e termina con
   "NASM not found" se non lo trova): lo installiamo esplicitamente.

Lo script e' idempotente: se prepare.py e' gia' corretto non cambia nulla e
esce con 0.
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


def sub(old, new, label):
    """Sostituzione idempotente: se il nuovo testo c'e' gia', non fa nulla."""
    global s
    if old in s:
        s = s.replace(old, new)
        applied.append(label)
    elif new.strip() and new.split('\n')[0] in s:
        pass  # gia' applicato in un run precedente
    else:
        print('ATTENZIONE: non trovato il testo per "%s"' % label)


# --- 1) diffutils: dal repo mingw64 (rimosso) al repo msys --------------------
sub('        mingw-w64-x86_64-diffutils ^\n', '', 'rimosso mingw64 diffutils')

# --- 2) un solo bash -c con tutti i pacchetti ---------------------------------
old_bash = (
    '    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"\n'
)
new_bash = (
    '    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm; '
    'pacman -S --noconfirm --needed diffutils make; '
    'pacman -S --noconfirm --needed mingw-w64-x86_64-nasm '
    'mingw-w64-x86_64-gperf mingw-w64-x86_64-perl '
    'mingw-w64-x86_64-pkgconf"\n'
)
# la vecchia versione (due righe separate) va comunque sostituita se presente
old_two_lines = (
    old_bash + '    pacman -S --noconfirm --needed diffutils\n'
)
sub(old_two_lines, new_bash, 'pacchetti msys in un solo bash -c')
sub(old_bash, new_bash, 'pacchetti msys in un solo bash -c')

# --- 3) Perl portable di riserva (OpenSSL ha bisogno di perl.exe) ------------
sp_anchor = "stage('openssl3', \"\"\"\n"
if sp_anchor in s and "stage('strawberry-perl'" not in s:
    strawberry = (
        "stage('strawberry-perl', \"\"\"\n"
        "win:\n"
        "    powershell -Command \"iwr -OutFile ./strawberry.zip "
        "https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/"
        "SP_54231_64bit/strawberry-perl-5.42.3.1-64bit-portable.zip\"\n"
        "    powershell -Command \"Expand-Archive ./strawberry.zip "
        "-DestinationPath ./perl-portable -Force\"\n"
        "    SET PATH=%THIRDPARTY_DIR%\\\\perl-portable\\\\perl\\\\bin;"
        "%THIRDPARTY_DIR%\\\\perl-portable\\\\c\\\\bin;%PATH%\n"
        "\"\"\", 'ThirdParty')\n\n"
    )
    s = s.replace(sp_anchor, strawberry + sp_anchor, 1)
    applied.append('stage strawberry-perl')

# OpenSSL: usa il perl portable se c'e'
for target in ('debug-VC-WIN64A /FS', 'VC-WIN64A /FS',
               'debug-VC-WIN32 /FS', 'VC-WIN32 /FS',
               'debug-VC-WIN64-ARM /FS', 'VC-WIN64-ARM /FS /Gs4096'):
    line = '    perl Configure no-shared no-tests %s\n' % target
    guard = ('    IF EXIST %THIRDPARTY_DIR%\\perl-portable\\perl\\bin\\perl.exe '
             'SET PATH=%THIRDPARTY_DIR%\\perl-portable\\perl\\bin;%PATH%\n')
    if line in s and guard + line not in s:
        s = s.replace(line, guard + line)
        applied.append('perl portable in OpenSSL (%s)' % target.split()[0])

# --- scrittura ---------------------------------------------------------------
if s == original:
    print('prepare.py gia\' corretto: nessuna modifica necessaria.')
else:
    with open(PREPARE, 'w', encoding='utf-8', newline='\n') as f:
        f.write(s)
    print('Modifiche applicate: ' + ', '.join(applied))

# --- verifiche --------------------------------------------------------------
checks = {
    'niente pacchetto mingw64 diffutils': 'mingw-w64-x86_64-diffutils' not in s,
    'diffutils dal repo msys': 'pacman -S --noconfirm --needed diffutils' in s,
    'nasm installato (serve a OpenSSL)': ('mingw-w64-x86_64-nasm' in s) or (
        re.search(r'pacman -S --noconfirm --needed[^\n]*\bnasm\b', s) is not None),
    'pacchetti msys in un solo bash -c': s.count('bash -c "pacman-key --init') == 1,
    'strawberry perl disponibile': "stage('strawberry-perl'" in s,
}
for name, ok in checks.items():
    print(('OK   ' if ok else 'FAIL ') + name)
if not all(checks.values()):
    sys.exit(1)
print('prepare.py corretto.')
