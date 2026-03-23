"""Ramulator2 CLI.

Usage:
    python -m ramulator <config_script.py>              # run simulation
    python -m ramulator run <config_script.py>           # run simulation (explicit)
    python -m ramulator export <config_script.py>        # export config as YAML
    python -m ramulator export <config_script.py> -f json # export as JSON
    python -m ramulator export <config_script.py> -o out.yaml
    python -m ramulator codegen                          # generate C++ from Python DSL
    python -m ramulator codegen DDR4                     # generate specific standard
    python -m ramulator codegen --dry-run                # print without writing
"""

import runpy
import sys


def run_main(args):
    """Run a config script as a simulation."""
    if not args:
        print("Usage: python -m ramulator [run] <config_script.py>", file=sys.stderr)
        sys.exit(1)
    script = args[0]
    sys.argv = args
    runpy.run_path(script, run_name="__main__")


def export_main(args):
    """Export config from a script without running simulation."""
    import argparse

    from ramulator.export import capture_config, dict_to_json, dict_to_yaml

    parser = argparse.ArgumentParser(
        prog="python -m ramulator export",
        description="Export a fully-expanded Ramulator2 config from a Python script.",
    )
    parser.add_argument("script", help="Config script (.py)")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument(
        "-f",
        "--format",
        choices=["yaml", "json"],
        default="yaml",
        help="Output format (default: yaml)",
    )
    opts = parser.parse_args(args)

    config = capture_config(opts.script)

    if opts.format == "json":
        text = dict_to_json(config)
    else:
        text = dict_to_yaml(config)

    if opts.output:
        with open(opts.output, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(1)

    cmd = sys.argv[1]
    if cmd == "export":
        export_main(sys.argv[2:])
    elif cmd == "codegen":
        from ramulator.codegen import codegen_main

        codegen_main(sys.argv[2:])
    elif cmd == "run":
        run_main(sys.argv[2:])
    else:
        # Backwards compat: no subcommand = run
        run_main(sys.argv[1:])


if __name__ == "__main__":
    main()
