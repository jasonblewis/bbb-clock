"""Entry point: `python3 -m telemetry_service [config-path]`."""

from __future__ import annotations

import argparse
import logging
import sys

from . import config
from .service import TelemetryService


def main(argv=None):
    parser = argparse.ArgumentParser(prog="telemetry_service")
    parser.add_argument(
        "config",
        nargs="?",
        default=config.DEFAULT_CONFIG_PATH,
        help=f"path to the INI config (default: {config.DEFAULT_CONFIG_PATH})",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    try:
        cfg = config.load(args.config)
    except config.ConfigError as exc:
        print(f"config error: {exc}", file=sys.stderr)
        return 2

    TelemetryService(cfg).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
