#!/usr/bin/env python3
"""Compatibility entry point; repair policy and validation live in agent_repair."""
from agent_repair import *  # noqa: F401,F403

if __name__ == '__main__':
    import sys
    sys.exit(main())
