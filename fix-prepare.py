#!/usr/bin/env python3
"""Rende preparabile AyuGram con i repository MSYS2/MSYS 2026 e con la shell
del runner GitHub (non interattiva).

Eseguito dal workflow PRIMA della build: modifica
Telegram/build/prepare/prepare.py con sostituzioni chirurgiche e verifica che
siano state applicate.

Il problema principale (visto nel log: "To complete this update all MSYS2
processes including this terminal will be closed ... has been terminated"):

    pacman -Syu uccide la shell che lo esegue (serve a sostituire msys2-runtime).
    Se lo si lancia nella stessa riga bash degli altri pacchetti, i pacchetti
    successivi NON vengono installati, ma prepare.py riceve un exit code 0
    "sporco" e va avanti. Risultato: nessun nasm/perl/gperf/make, nessun errore
    visibile, e OpenSSL fallisce piu' tardi.

Soluzione: separare l'aggiornamento del sistema (che puo' "fallire", e' normale)
dall'installazione dei pacchetti (tutta in una riga sola), e fare in modo che il
fallimento dell'aggiornamento non blocchi lo script.
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

# --- nuovo blocco di comandi per lo stage msys64 -----------------------------
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

# 1) togli il vecchio pacchetto mingw64 diffutils (inesistente dal 2026)
if '        mingw-w64-x86_64-diffutils ^\n' in s:
    s = s.replace('        mingw-w64-x86_64-diffutils ^\n', '')
    applied.append('rimosso mingw-w64-x86_64-diffutils')

# 2) sostituisci la riga bash "tutto in uno" con tre comandi separati.
#    Copre sia la versione originale di AyuGram sia quella della v5-v7.
patterns = [
    # v5-v7: una sola riga con tutti i pacchetti
    re.compile(r'    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm[^"\n]*"\n'),
    # originale AyuGram
    re.compile(r'    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"\n'
               r'(?:    pacman -S --noconfirm --needed diffutils\n)?'),
]
for pat in patterns:
    if pat.search(s):
        s = pat.sub(NEW_BLOCK, s, count=1)
        applied.append('pacman: aggiornamento separato dai pacchetti')
        break

# 2b) rimuovi il vecchio blocco pacman multi-riga (fa un secondo -Syu che puo'
#     terminare di nuovo la shell): i pacchetti sono gia' nella riga singola
old_multiline = re.compile(r'    pacman -Syu --noconfirm \^\n(?:        [^\n]*\n)+')
if old_multiline.search(s):
    s = old_multiline.sub('', s, count=1)
    applied.append('rimosso vecchio blocco pacman multi-riga')

# 2c) evita la riga SET duplicata
s = s.replace('    SET MSYS2_PATH_TYPE=inherit\n    SET MSYS2_PATH_TYPE=inherit\n',
              '    SET MSYS2_PATH_TYPE=inherit\n')

# 3) NIENTE DOMANDE INTERATTIVE ------------------------------------------------
#    Nel log: "[5/35](ThirdParty/jom): CHANGED, (r)ebuild, rebuild (a)ll, ..."
#    prepare.py chiede conferma quando la cartella esiste gia' ma la chiave di
#    cache non combacia (succede quando uno stage precedente e' stato
#    interrotto). Su un runner GitHub non c'e' nessuno a rispondere: lo script
#    resta fermo e poi muore. Si rende la scelta automatica ("rebuild").
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
elif 'CHANGED: ricostruisco automaticamente' not in s:
    print('ATTENZIONE: prompt interattivo non trovato (prepare.py cambiato?)')

# 3) Perl portable di riserva (OpenSSL ha bisogno di perl.exe)
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

# 4) OpenSSL: usa il perl portable se c'e'
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
pacman_line = re.search(r'    bash -c \"pacman -S --noconfirm --needed ([^\"]+)\"', s)
packages = pacman_line.group(1).split() if pacman_line else []
checks = {
    'niente pacchetto mingw64 diffutils': 'mingw-w64-x86_64-diffutils' not in s,
    'aggiornamento MSYS2 separato e non bloccante':
        re.search(r'bash -c "pacman -Syu --noconfirm" \|\| ', s) is not None,
    'pacchetti installati in una sola riga':
        all(p in packages for p in ('diffutils', 'make', 'mingw-w64-x86_64-nasm',
                                    'mingw-w64-x86_64-gperf', 'mingw-w64-x86_64-perl')),
    'nasm installato (serve a OpenSSL)': 'mingw-w64-x86_64-nasm' in s,
    'strawberry perl disponibile': "stage('strawberry-perl'" in s,
    'nessun prompt interattivo': 'CHANGED: ricostruisco automaticamente' in s,
}
for name, ok in checks.items():
    print(('OK   ' if ok else 'FAIL ') + name)
if not all(checks.values()):
    sys.exit(1)
print('prepare.py corretto.')
