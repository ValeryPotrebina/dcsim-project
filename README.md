# dcSIM Simulator

A discrete-event network simulator for evaluating datacenter transport protocols, with a focus on the **dcSIM** algorithm (referred to as **SNS** in the codebase).

## Publication

**Simulate Before Sending: Rethinking Transport in Datacenter Networks**
Dan Straussman (Technion), Isaac Keslassy (Technion and UC Berkeley), Alexander Shpiner, Liran Liss (Nvidia).

Presented at the **NINeS Conference**, February 2026.

- [Paper](https://nines-conference.org/papers/p019-Straussman.pdf)
- [Presentation (Video)](https://vimeo.com/showcase/NINeS?video=1161891326)

## Overview

dcSIM is a **simulation-based scheduling** protocol for datacenter networks. The core idea is to send lightweight simulation (SIM) packets ahead of data to probe network path congestion, then schedule actual data transmission based on observed conditions.

### dcSIM (SNS) Algorithm

1. **SIM Phase** — The sender transmits small header-only simulation packets that traverse the network path to the receiver, probing congestion and queuing delays.
2. **SIM_ACK Phase** — The receiver replies with a SIM_ACK packet (acting as a bandwidth reservation) back to the sender.
3. **DATA Phase** — Upon receiving the SIM_ACK, the sender schedules the actual data packet at a calculated time (`sim_send_time + worst_case_RTT`), ensuring data arrives when the probed path is available.
4. **DATA_ACK Phase** — The receiver acknowledges received data.


## Supported Protocols

| ID  | Protocol         | Description                              |
|-----|------------------|------------------------------------------|
| 117 | **SNS (dcSIM)**  | Simulation-based scheduling              |
| 116 | PIM              | Multi-round distributed matching         |
| 115 | RUF              | Receiver-driven scheduling               |
| 114 | Fastpass          | Centralized scheduling                   |
| 112 | Capability        | ExpressPass-like credit-based            |
| 113 | Magic             | Ideal (oracle) scheduling                |
| 2   | pFabric           | Priority-based flow scheduling           |
| 42  | Vanilla TCP       | Standard TCP                             |
| 43  | DCTCP             | Data Center TCP                          |
| 120 | Ideal             | Ideal baseline with arbiter              |

## Building

### Prerequisites

- GCC (version 6 recommended)
- GNU Autotools (`autoconf`, `automake`, `aclocal`)

### Compile

```bash
aclocal
automake --add-missing
autoconf
./configure
make
```

> **Note:** If your GCC version is greater than 6, you may encounter compilation issues. Use GCC 6 explicitly:
>
> ```bash
> CC='gcc-6' CXX='g++-6' ./configure
> CC='gcc-6' CXX='g++-6' make
> ```

This produces two binaries:
- `simulator` — Optimized build (`-O3`)
- `simdebug` — Debug build (`-O0`)

## Usage

```bash
./simulator <exp_type> <conf_file>
```

- `exp_type`:
  - `1` — Run full simulation
  - `2` — Generate flows only (no simulation)
- `conf_file` — Path to a configuration file (key-value format)

### Configuration File Format

Plain text, one parameter per line (`key: value`). Lines starting with `#` or `//` are comments.

#### Core Parameters

| Parameter            | Description                                    | Example         |
|----------------------|------------------------------------------------|-----------------|
| `topology`           | `LeafSpine` or `FatTree`                       | `LeafSpine`     |
| `num_hosts`          | Number of hosts                                | `144`           |
| `bandwidth`          | Link bandwidth (bps)                           | `10000000000`   |
| `propagation_delay`  | Link propagation delay (s)                     | `0.0000002`     |
| `queue_size`         | Switch queue size (bytes)                      | `36864`         |
| `flow_type`          | Protocol ID (see table above)                  | `117`           |
| `queue_type`         | 1=DropTail, 2=pFabric, 5=DCTCP, 6=SNS         | `6`             |
| `load`               | Network load factor                            | `0.8`           |
| `num_flow`           | Number of flows to simulate                    | `100000`        |
| `flow_trace`         | CDF or flow trace file path                    | `trace.txt`     |

#### dcSIM (SNS) Specific Parameters

| Parameter                       | Description                                            |
|---------------------------------|--------------------------------------------------------|
| `big_mtu`                       | MTU size: 0 = 1500B, 1 = 4KB, 2 = 9KB (jumbo)        |
| `synced_mode`                   | 1 = synchronized scheduling, 0 = immediate            |
| `clock_drift`                   | Host clock drift for realistic timing                  |
| `worst_rrt_factor`              | Worst-case RTT multiplier                              |
| `debug_sns`                     | Enable SNS debug output                                |

## Project Structure

```
coresim/       Core simulator engine (events, topology, hosts, packets)
ext/           Protocol implementations (dcSIM/SNS, PIM, RUF, pFabric, TCP, etc.)
run/           Experiment runner, flow generators, configuration parsing
```

## Acknowledgments

This simulator is built upon the **dcPIM** simulator.
