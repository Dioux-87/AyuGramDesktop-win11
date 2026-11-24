#!/usr/bin/env python3
"""Rende preparabile AyuGram con i repository MSYS2/MSYS attuali.

Eseguito dal workflow PRIMA della build: modifica Telegram/build/prepare/prepare.py
con sostituzioni chirurgiche e verifica che siano state applicate.

1. `mingw-w64-x86_64-diffutils` non esiste più nei repo MSYS2 (2026): il pacchetto
   `diff` ora sta nel repo MSYS come `diffutils`.
2. Se il pacchetto Perl di MSYS2 continua a non essere disponibile, installa la
   versione portable di Strawberry Perl per OpenSSL.
"""
import os
import re
import sys

PREPARE = os.path.join('Telegram', 'build', 'prepare', 'prepare.py')

with open(PREPARE, encoding='utf-8') as f:
    original = f.read()
s = original

# --- 1) diffutils: dal repo mingw64 (rimosso) al repo msys (diffutils) --------
s = s.replace('        mingw-w64-x86_64-diffutils ^\n', '')
s = s.replace(
    '    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"\n',
    '    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"\n'
    '    pacman -S --noconfirm --needed diffutils\n',
)

# --- 2) Perl portable di riserva (OpenSSL ha bisogno di perl.exe) ------------
sp_anchor = "stage('openssl3', \"\"\"\n"
assert sp_anchor in s, 'anchor openssl3 non trovato'
strawberry = (
    "stage('strawberry-perl', \"\"\"\n"
    "win:\n"
    "    powershell -Command \"iwr -OutFile ./strawberry.zip https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_54231_64bit/strawberry-perl-5.42.3.1-64bit-portable.zip\"\n"
    "    powershell -Command \"Expand-Archive ./strawberry.zip -DestinationPath ./perl-portable -Force\"\n"
    "    SET PATH=%THIRDPARTY_DIR%\\\\perl-portable\\\\perl\\\\bin;%THIRDPARTY_DIR%\\\\perl-portable\\\\c\\\\bin;%PATH%\n"
    "\"\"\", 'ThirdParty')\n\n"
)
s = s.replace(sp_anchor, strawberry + sp_anchor, 1)

# OpenSSL: usa il perl portable se c'e'
s = s.replace(
    "    perl Configure no-shared no-tests debug-VC-WIN64A /FS\n",
    "    IF EXIST %THIRDPARTY_DIR%\\perl-portable\\perl\\bin\\perl.exe SET PATH=%THIRDPARTY_DIR%\\perl-portable\\perl\\bin;%PATH%\n"
    "    perl Configure no-shared no-tests debug-VC-WIN64A /FS\n",
)
s = s.replace(
    "    perl Configure no-shared no-tests VC-WIN64A /FS\n",
    "    IF EXIST %THIRDPARTY_DIR%\\perl-portable\\perl\\bin\\perl.exe SET PATH=%THIRDPARTY_DIR%\\perl-portable\\perl\\bin;%PATH%\n"
    "    perl Configure no-shared no-tests VC-WIN64A /FS\n",
)

if s == original:
    print('ERRORE: nessuna sostituzione applicata (prepare.py e cambiato?)')
    sys.exit(1)

with open(PREPARE, 'w', encoding='utf-8', newline='\n') as f:
    f.write(s)

# --- verifiche --------------------------------------------------------------
checks = {
    'niente pacchetto mingw64 diffutils': 'mingw-w64-x86_64-diffutils' not in s,
    'diffutils dal repo msys': 'pacman -S --noconfirm --needed diffutils' in s,
    'strawberry perl disponibile': "stage('strawberry-perl'" in s,
}
for name, ok in checks.items():
    print(('OK   ' if ok else 'FAIL ') + name)
if not all(checks.values()):
    sys.exit(1)
print('prepare.py corretto.')
