FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/.cargo/bin:${PATH}"

# Base dependencies
RUN apt-get update && apt-get install -y \
    build-essential curl wget bc time python3 python3-pip unzip \
    miller datamash cargo rustc \
    && rm -rf /var/lib/apt/lists/*

# Install csvkit (Python-based CSV toolkit)
RUN pip3 install --no-cache-dir csvkit

# Install xsv from source using distro cargo/rustc.
RUN cargo install xsv

# Install xan, the modern Rust CSV CLI used by the benchmark comparator.
RUN wget -qO /tmp/xan.tar.gz https://github.com/medialab/xan/releases/download/0.57.1/xan-x86_64-unknown-linux-musl.tar.gz \
    && echo "2651285cc04d859c0a19dd2a190e2f2223b3e2659bcd82f90d87c4f2422a9943  /tmp/xan.tar.gz" | sha256sum -c - \
    && tar xzf /tmp/xan.tar.gz -C /usr/local/bin/ \
    && chmod +x /usr/local/bin/xan \
    && rm /tmp/xan.tar.gz

# Install qsv (prebuilt musl binary - statically linked, no external deps)
RUN wget -qO /tmp/qsv.zip https://github.com/dathere/qsv/releases/download/14.0.0/qsv-14.0.0-x86_64-unknown-linux-musl.zip \
    && unzip -q /tmp/qsv.zip -d /tmp/qsv \
    && mv /tmp/qsv/qsv /usr/local/bin/ \
    && chmod +x /usr/local/bin/qsv \
    && rm -rf /tmp/qsv /tmp/qsv.zip

# Install frawk (prebuilt binary - no-llvm variant)
RUN wget -qO /tmp/frawk.tar.gz https://github.com/ezrosent/frawk/releases/download/v0.4.1/linux-x86-musl-no-llvm.tar.gz \
    && tar xzf /tmp/frawk.tar.gz -C /usr/local/bin/ \
    && chmod +x /usr/local/bin/frawk \
    && rm /tmp/frawk.tar.gz

# Install csvtk (Go-based, prebuilt binary)
RUN wget -qO- https://github.com/shenwei356/csvtk/releases/latest/download/csvtk_linux_amd64.tar.gz \
    | tar xz -C /usr/local/bin/

# Install tsv-utils (D-based, prebuilt binary)
RUN wget -qO- https://github.com/eBay/tsv-utils/releases/download/v2.1.1/tsv-utils-v2.1.1_linux-x86_64_ldc2.tar.gz \
    | tar xz --strip-components=2 -C /usr/local/bin/

# Install goawk
RUN wget -qO- https://github.com/benhoyt/goawk/releases/download/v1.31.0/goawk_v1.31.0_linux_amd64.tar.gz \
    | tar xz -C /usr/local/bin/

WORKDIR /benchmark
COPY . /benchmark/cisv/

# Build cisv CLI
WORKDIR /benchmark/cisv
RUN make clean || true && make core cli

COPY cli/benchmarks/run_benchmark.sh /benchmark/
COPY cli/benchmarks/run_merge_benchmark.sh /benchmark/
COPY cli/benchmarks/generate_rfc_csv_corpus.sh /benchmark/
RUN chmod +x /benchmark/run_benchmark.sh /benchmark/run_merge_benchmark.sh /benchmark/generate_rfc_csv_corpus.sh

WORKDIR /benchmark
ENTRYPOINT ["/benchmark/run_benchmark.sh"]
CMD ["--rows=1000000", "--cols=7", "--iterations=5"]
