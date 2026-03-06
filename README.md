# cisv-cli

![License](https://img.shields.io/badge/license-MIT-blue)

CLI distribution for CISV.

## FEATURES

- Fast CSV parsing/counting from shell
- Column selection and ranged reads
- Benchmark mode and writer subcommand
- Same parser core as `cisv-core`

## INSTALLATION

```bash
git clone --recurse-submodules https://github.com/Sanix-Darker/cisv-cli
cd cisv-cli
make all
sudo make install
```

## CORE DEPENDENCY (SUBMODULE)

This repository tracks `cisv-core` via the `./core` git submodule.

To fetch the latest `cisv-core` (main branch) in your local clone:

```bash
git submodule update --init --remote --recursive
```

CI and release workflows also run this update command, so new `cisv-core` releases are pulled automatically during builds.

## CLI USAGE

```bash
cisv [OPTIONS] FILE
```

Key options:

- `-c, --count`: count rows only
- `-s, --select`: select columns by index
- `--from-line`, `--to-line`: parse range
- `-d, --delimiter`: custom delimiter
- `-t, --trim`: trim whitespace

## EXAMPLES

### BASIC

```bash
./cli/build/cisv examples/sample.csv
./cli/build/cisv -c examples/sample.csv
```

### DETAILED

```bash
./cli/build/cisv -s 0,2 examples/sample.csv
./cli/build/cisv --from-line 2 --to-line 3 examples/sample.csv
```

More runnable scripts: [`examples/`](./examples)

## BENCHMARKS

```bash
docker build -t cisv-cli-bench -f cli/benchmarks/Dockerfile .
docker run --rm --platform linux/amd64 --cpus=2 --memory=4g cisv-cli-bench
```

![CLI Benchmarks](./assets/benchmark-cli.png)

## UPSTREAM CORE

- cisv-core: https://github.com/Sanix-Darker/cisv-core
