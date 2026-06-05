
# GhostDAG Simulator

A network simulator for studying efficient propagation in P2P blockchain networks with DAG (Directed Acyclic Graph) structure. Built on top of ns-3 with MPI support for distributed simulation.

## Overview

This project simulates the GHOSTDAG consensus protocol, which extends traditional blockchain by allowing parallel blocks while maintaining a consistent ordering through a DAG structure (See [Phantom Ghostdag](https://dl.acm.org/doi/pdf/10.1145/3479722.3480990)). The simulator enables researchers to analyze network propagation characteristics, block dissemination patterns, and consensus performance under various network conditions.

Based on [Bitcoin-Simulator by Arthur Gervais](https://github.com/arthurgervais/Bitcoin-Simulator) with custom GHOSTDAG consensus implementation and transaction propagation handling.

## Features

- **GHOSTDAG Consensus**: Full implementation of the GHOSTDAG ordering algorithm with blue/red block classification
- **Distributed Simulation**: MPI-based parallel simulation for scalability
- **Realistic Network Topology**: Geographic node distribution with regional latency modeling (based on bitcoin real topology by Arthur Gervais work)
- **Transaction Propagation**: Inv-based transaction dissemination with batching
- **Block Propagation**: Multi-parent block relay with bandwidth-aware handling

## Requirements

- ns-3.46+ with MPI support
- CMake 3.10+
- MPI implementation (Open MPI or similar)
- C++17 compatible compiler

## Quick Start
Clone or download this project on ns3 distribution folder at `ns3-<dist>/ns3-<version>/scratch/`.
And then configure and run using ns3 facilities:

```bash
# Build with ns3 waf
./ns3 configure --enable-mpi -- -DGHOSTDAGSIM_METRICS=ON #if you want metrics to be build with
./ns3 build ghostdagsim

# Run simulation
./ns3 run ghostdagsim -- <flags>

# Run with MPI
./ns3 run --command-template "mpirun -np <number of mpi threads> %s" ghostdagsim -- <flags>
```

You can also build and run with docker
> Or you can use the pre built docker image [RELEASE](https://github.com/lechinskie/ghostdagsim/releases) if you just wanna run changing the parameters, not the code.
```bash
docker build -t ghostdagsim .

docker run [-v $(pwd):/results] [-e MPI_THREADS=4] ghostdagsim -- <flags>
#          ^ Opt Map folder      ^ Opt (Default 1)
# you can pass also any mpi parameters with -e MPI_ARGS="args..."
```


## Configuration

Simulation parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| `--nodes` | Total number of nodes | 10 |
| `--miners` | Number of mining nodes | 10 |
| `--min_conn` | Minimum connections per node | -1 (no restriction > 0) |
| `--max_conn` | Maximum connections per node | -1 (no restriction <= nodes)|
| `--lambda` | Mean block interval per miner (seconds) | 20.0 |
| `--tau` | Propagation delay multiplier | 1.0 |
| `--pareto_divider` | Propagation latency Pareto distribution shape divider | 5.0 |
| `--k` | GHOSTDAG k parameter | 10 |
| `--txs_per_block` | Transactions per block | 100 |
| `--mempool_size` | Mempool size | 10000 |
| `--tx_fee_lambda` | Mean transaction fee (exponential distribution) | 150.0 |
| `--tx_gen_interval` | Mean transaction generation interval per node (seconds) | 0.5 |
| `--blocks_per_miner` | Target number of blocks each miner should produce | 1000 |
| `--run_name` | Name tag for this simulation run | "run0" |
| `--graphene` | Use graphene relay handler | false |

## Output

Simulation results are written to `results/<run name>/<mpi rank>` including:
- Block mining and reception events
- DAG snapshots and coloring
- Transaction propagation metrics
- Network message traces
- Configuration summary

## Project Structure

```
ghostdagsim/
├── main.cc          # Simulation entry point and configuration
├── node.{cc,h}      # Node application and network handling
├── dag.{cc,h}       # GHOSTDAG consensus implementation
├── graphene.{cc,h}  # Graphene protocol specifics implementation
├── miner.{cc,h}     # Mining node functionality
├── mempool.{cc,h}   # Transaction mempool management
├── metrics.{h}      # Metrics collection and output
├── helpers/         # Topology and network helpers
└── tests/           # Unit tests
```

## Contributing

Feel completely free to send me an email (lechinski@univali.br) asking questions about the project. Any contribution researching and implementing new algorithms and methods are welcome.

Here are some additions that i find interesting:
  - Test network stress under different topologies
  - Forks changing consensus layer researching differences between network usage and efficiency of optimization techniques
  - A plugin system on message handler/sender for research more methods on propagation
  - Generalized metrics in consensus protocol
  - Attacker modeling and tip selection strategies
  - Network resilience and security metrics


For run tests you can just use ns3 as well:
> [!WARNING]
> Be sure to have GTest installed and configured ns3 with -DGHOSTDAGSIM_TESTS=ON

```bash
# tests are ns3 scratchs that use the structures and asserts with gtest
./ns3 run <testname>
```

## License

This project is licensed under **GPL-2.0** (see [LICENSE](LICENSE))

### Third-party components

- **Network topology & node architecture**: Derived from
  [Bitcoin-Simulator](https://github.com/arthurgervais/Bitcoin-Simulator) by
  Arthur Gervais et al., also GPL-2.0.

- **Mempool** (`mempool.h`, `mempool.cc`): Originally from
  [DAG-Sword](https://github.com/Tem12/DAG-simulator) by Hladký, Perešíni et al.
  (BUT Security@FIT, 2021–2022), distributed under their BSD license with GPL-2.0.

- **json.hpp** (`thirdparty/json.h`): [nlohmann/json](https://github.com/nlohmann/json)
  by Niels Lohmann, MIT License. Included unmodified.

- **Bloom filter** (`thirdparty/bloom_filter.h`): [ArashPartow/bloom](https://github.com/ArashPartow/bloom)
  by ArashPartow, MIT License. Included unmodified.

- **IBLT** (`thirdparty/iblt.h`): [umass-forensics/IBLT-optimization](https://github.com/umass-forensics/IBLT-optimization) and [gavinandresen/IBLT_Cplusplus](https://github.com/gavinandresen/IBLT_Cplusplus).
  Originally IBLT_Cplusplus rewrite by graphene research at umass-forensics and changed to header-library only in this work, MIT License (untouched). Included and modified.

### Attribution

If you use this simulator in your research, please cite or link to this repository.
Academic contributions and forks are welcome — just keep the GPL-2.0 license and
preserve the attribution headers in the source files.
