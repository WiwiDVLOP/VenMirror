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
    version_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "../QtScrcpy/appversion"))
    file=open(version_file, 'w')
    file.write(version)
    file.close()
    sys.exit(0)