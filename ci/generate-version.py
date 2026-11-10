import sys
import os

if __name__ == '__main__':
    p = os.popen('git rev-list --tags --max-count=1')
    commit = p.read()
    p.close()

    p = os.popen('git describe --tags ' + commit)
    tag = p.read().strip()
    p.close()

    # No tags yet (fresh fork): fall back to the development version.
    # Without this, appversion ends up empty and the CMake project()
    # call fails on VERSION.
    version = str(tag[1:]) if tag else "1.0.0"
    # CMake splits the version into MAJOR/MINOR/PATCH and the .rc file
    # needs all three numeric: a tag like v1.0 would leave PATCH empty
    # and break the Windows resource compiler (RC2127/RC2167).
    version = version.split('-')[0]  # drop any -N-gSHA suffix
    parts = version.split('.')
    while len(parts) < 3:
        parts.append('0')
    version = '.'.join(parts[:3])
    version_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "../QtScrcpy/appversion"))
    file=open(version_file, 'w')
    file.write(version)
    file.close()
    sys.exit(0)