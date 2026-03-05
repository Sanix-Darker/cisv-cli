# cisv-cli

CLI distribution for CISV.

## Upstream core

- cisv-core: https://github.com/Sanix-Darker/cisv-core

## Build

```bash
make all
```

## Test

```bash
make test
```

## Benchmark Docker

```bash
docker build -t cisv-cli-bench -f cli/benchmarks/Dockerfile .
docker run --rm --platform linux/amd64 --cpus=2 --memory=4g cisv-cli-bench
```
