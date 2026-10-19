#!/usr/bin/env python3
"""Validate a native build's complete recorded runtime before fixture side effects."""
import argparse
import json
from pathlib import Path
import sys
import engine


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-record', required=True)
    parser.add_argument('--executable', required=True)
    args = parser.parse_args()
    try:
        record = Path(args.build_record).resolve(strict=True)
        details = json.loads(record.read_text())['details']
        executable = Path(args.executable).resolve(strict=True)
        if Path(details['binary']).resolve(strict=True) != executable:
            engine.fail('Runtime browser path differs from the build record')
        app = next((parent for parent in executable.parents if parent.suffix == '.app'), None)
        output = app.parent if app else executable.parent
        manifest = engine.require_runtime(details.get('runtime'), output, executable, record.parent)
        print(json.dumps({'entries': len(manifest['files'])}))
    except (RuntimeError, OSError, ValueError, KeyError, TypeError) as error:
        print('Native runtime: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
