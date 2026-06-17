# Simulate Before Sending: Rethinking Transport in Datacenter Networks

**Network Computing — Project Report**
Politecnico di Milano, Academic Year 2025–26

---

**Team Members:**
Valeriia Potrebina — valeriia.potrebina@mail.polimi.it
Alankar Gupta — alankar.gupta@mail.polimi.it
Edoardo Storti — edoardo1.storti@mail.polimi.it

---

**Source Paper:**
Dan Straussman (Technion, Israel), Isaac Keslassy (Technion & UC Berkeley), Alexander Shpiner (Nvidia), Liran Liss (Nvidia).
*"Simulate Before Sending: Rethinking Transport in Datacenter Networks"*, NINeS 2026.

**Project Repository:** https://github.com/ValeryPotrebina/dcsim-project


## Table of Contents

1. [Introduction](#1-introduction)
2. [Selected Result](#2-selected-result)
3. [Environment Setup](#3-environment-setup)
4. [Experiment Result](#4-experiment-result)
5. [Further Exploration — dcSim vs dcPIM on Non-AI (Web-Search) Traffic](#5-further-exploration--dcsim-vs-dcpim-on-non-ai-web-search-traffic)
6. [Reproducibility Assessment of the Paper](#6-reproducibility-assessment-of-the-paper)
7. [Conclusion](#7-conclusion)
- [Appendix A: dcPIM Protocol Summary](#appendix-a-dcpim-protocol-summary)
- [Appendix B: Why Capability Transport Was Used for pHost](#appendix-b-why-capability-transport-was-used-for-phost)
- [Appendix C: Code Changes Made to dcPIM and dcSim](#appendix-c-code-changes-made-to-dcpim-and-dcsim)

---

## 1. Introduction

The paper addresses the behaviour of AI training communications in datacenters during model training. AI training traffic — particularly collective operations like AllReduce and All-to-All — is inherently bursty and unpredictable, and causes significant packet drops that harm performance in two ways: packet retransmissions increase Collective Completion Times (CCTs), and the tail latency of RDMA grows because RDMA assumes a lossless network.

Prior transport protocols fail at least one of the following requirements: low CCT, near-zero loss, no specialised hardware, robustness to variable load, and scalability. dcSim addresses all of them at the same time.

The key idea is to *simulate traffic before sending it*. Before each Data packet is transmitted, a small Sim (simulation) probe — 64 bytes — is sent along the same path through a dedicated shadow buffer partition on commodity switches. If the Sim-Ack returns successfully, the path is clear and the Data packet follows after a fixed delay `RTT_max`. If the Sim is dropped, the Data packet is held back. Because Sim packets are 100× smaller than Data packets and use dedicated buffers, a Sim drop reliably predicts that a Data packet would also have been dropped — without actually losing any Data. The result is a practically lossless network using only commodity hardware.

The paper evaluates dcSim against dcPIM, pHost, and pFabric on AI training collectives (permutation, all-to-all, all-to-all-v) and shows it achieves faster CCTs with near-zero loss and reordering.

---

## 2. Selected Result

We selected **Figure 5(b)** from the paper: CCT for a single all-to-all collective under four flow sizes (0.5×, 1×, 2×, 4× BDP), comparing dcSim, dcPIM, pHost, and pFabric.

The all-to-all pattern models tensor parallelism — every host sends a flow to every other host at the same time, giving 128 × 127 = 16,256 concurrent flows. This is the most congested and demanding collective in the paper, which made it the most informative case for us to try to reproduce.

The paper claims:

> "dcSim completes faster than dcPIM, pFabric and pHost in all experiments, except for a half-BDP size in the permutation traffic." (§4.2)

We reproduced Figure 5(b) at 0.5×, 1×, and 2× BDP for dcSim, dcPIM, and pHost. The qualitative trend held: dcSim is consistently faster than the baselines, all protocols show CCT scaling correctly with flow size, and dcSim observed zero packet drops and zero reordering across all flow sizes — directly confirming the paper's lossless claim.

---

## 3. Environment Setup

### Hardware Environment

All simulations ran inside a Docker container on a standard development laptop. The simulator is single-threaded and CPU-bound; no GPU, cluster, or special hardware was needed. Each simulation run took between 2 and 15 minutes depending on the protocol and flow size.

### Software Environment

- OS: Ubuntu 18.04 LTS
- Compiler: GCC 6 (`g++-6`) — required for compatibility with the simulator's C++ codebase; newer compilers produce build errors
- Build system: GNU Autotools (`autoconf`, `automake`, `libtool`, `make`)
- Python 3 with `matplotlib` and `numpy` for trace generation and plotting
- Simulator: dcSim public artifact at https://github.com/danstr1/dcsim (as of May 2026)
- Helper scripts for trace generation, simulation orchestration, and plotting are in the project repository under `scripts/`

### Topology and Workload

- Fat-Tree k=8, 128 hosts, 800 Gbps per link, 9 KB jumbo frames (`big_mtu: 2`)
- Propagation delay: 200 ns per link; queue size: 500 KB per port
- Switch processing latency: 450 ns (per paper §4.1), zero-load RTT: 7.8 µs
- All-to-all collective: 16,256 flows, all starting at t=0
- Flow sizes: 0.5× BDP (43 pkts), 1× BDP (87 pkts), 2× BDP (174 pkts); BDP = 87 packets
- Number of runs: 1 per data point (the paper averages over 20 runs)
- 4× BDP was not run — the simulation ran out of memory and crashed the host machine under the load of 16,256 simultaneous large flows; we report only 0.5×, 1×, and 2× BDP

### dcSim Configuration

`flow_type: 117`, `queue_type: 6`, `host_type: 17`

```
synced_mode: 1
param_b: 12
l_mult: 1
worst_rrt_factor: 1.0
clock_drift: 0.0
num_data_packets_per_sim_ack: 1
init_cwnd: 12
max_cwnd: 87
retx_timeout: 0.0001
```

### dcPIM Configuration

`flow_type: 116`, `queue_type: 1`, `host_type: 16`

```
pim_iter_limit: 4
pim_alpha: 1.0
pim_beta: 1.3
pim_k: 4
pim_select_min_iters: 1
token_initial: 0
token_timeout: 10
token_resend_timeout: 8
token_window: 6
token_window_timeout: 1.1
init_cwnd: 12
max_cwnd: 87
retx_timeout: 0.0001
```

### pHost Configuration

`flow_type: 112`, `queue_type: 2`, `host_type: 12`

```
init_cwnd: 12
max_cwnd: 87
retx_timeout: 0.0001
capability_timeout: 5
capability_resend_timeout: 1
capability_initial: 0
capability_window: 5
capability_window_timeout: 1
```

**Why `flow_type: 112` for pHost:** the original dcPIM repository (`simulator/py/fat_tree/fat_tree.py`, `conf_str_phost`) uses `flow_type: 112`, `host_type: 12` (capability transport) as pHost. The native RUF implementation (`flow_type: 115`, `host_type: 15`) is a separate protocol also designed for 10 Gbps. We used `flow_type: 112` to match what the original dcPIM repo does for pHost.

**pHost parameters were tuned by hand.** The paper publishes no pHost configuration for 800 Gbps, and the artifact has no documentation for the capability transport parameters at this link speed. The original dcPIM repo scripts target 10 Gbps, where BDP is far smaller and these parameters behave quite differently. We tuned `capability_window`, `capability_timeout`, `capability_resend_timeout`, and `capability_window_timeout` by running repeated simulations and adjusting values until CCT scaled with flow size and fell within the range visible in the paper's Figure 5(b). The values above are the result of that manual search — not values from any published source.

### Trace File Format

The paper does not publish trace generation scripts. We reverse-engineered the format from the `FlowReader` C++ class:

```
flow_id  start_time  collective_type  collective_id  size_in_pkts  0  0  src  dst
```

Each of the 128 × 127 = 16,256 `(src, dst)` pairs is assigned one flow with `start_time = 0.0`. The `collective_id` field is set to 0 for all flows, and CCT is computed as `max(FCT)` across all rows in the output file.

### Deviations from the Original Setup

- 1 run per data point vs. the paper's 20-run average; our results have higher variance
- `token_window: 6` for dcPIM was found empirically; the paper does not document this parameter
- pHost capability parameters were tuned empirically — the paper publishes no pHost config for 800 Gbps and no documentation exists; results are a best-effort approximation
- pFabric could not be reproduced correctly (see Section 4.2 Challenge 8); it is omitted from our figure
- Switch processing latency required manual investigation to reproduce (see Section 4.2 Challenge 1)

---

## 4. Experiment Result

### 4.1 Execution Procedure

For each protocol and flow size we did the following:

1. Generate the all-to-all trace file (Python script, 16,256 flows)
2. Run `./simulator 1 <config_file>` inside the Docker container
3. Compute CCT as `max(FCT)` across all output rows in `fct.txt`
4. Plot alongside the paper's Figure 5(b) values

### 4.2 Debugging and Challenges

Reproducing Figure 5(b) needed significantly more effort than we expected. The artifact's README covers only dcSim. Every other protocol required us to reverse-engineer the C++ source.

#### Challenge 1: dcSim results initially wrong — broken `RTT_max` computation

Our first dcSim runs gave CCT values very different from the paper. The root cause was in `SnsFatTreeTopology::get_worst_rtt()` — the function computing `RTT_max`, the parameter that controls how long dcSim waits after sending a Sim probe before sending the corresponding Data packet.

`RTT_max` is central to dcSim's correctness. The paper gives it directly in Theorem 2:

$$RTT_{max} = RTT_p + \frac{2H \cdot \ell}{C} \cdot ((B+1) \cdot \alpha + 2.1B + 1)$$

where $RTT_p$ is the maximum propagation and processing time, $H = 6$ hops, $\ell = 64$ bytes (Sim size), $C = 800$ Gbps, $B = 12$ (Sim buffer), $\alpha = L/\ell = 9000/64 \approx 141$.

The paper states that each switch has a 500 KB buffer per port and a processing latency of 450 ns, which gives a zero-load RTT of 7.8 µs. So $RTT_p = 7.8\,\mu\text{s}$, giving:

$$RTT_{max} = 7.8 + \frac{2 \times 6 \times 64 \times 8}{800 \times 10^9} \times (13 \times 141 + 25.2 + 1) \approx 7.8 + 14.2 = 22\;\mu\text{s}$$

The original `get_worst_rtt()` ignored switch processing latency entirely, which gave a zero-load RTT of only ~2.4 µs instead of 7.8 µs and made `RTT_max` approximately 3× too small (~7 µs instead of ~22 µs). Rather than try to patch the broken formula, we implemented Theorem 2 directly using `params.rtt` (which already includes the switch processing term via a hardcoded `+ 0.0000045` representing 10 switches × 450 ns) plus the queueing term from the theorem.

#### Challenge 2: All-to-all trace files not provided

The paper evaluates collective workloads but the artifact contains no trace generation scripts. The `FlowReader` class expects traces in a specific format, but the format is not documented anywhere. We reverse-engineered it from the C++ source of `FlowReader` and wrote our own Python generator (in `scripts/`).

#### Challenge 3: dcPIM — all 16,256 flows timing out

When we first ran dcPIM, every flow produced FCT = 10⁶ µs — the simulator's 1-second timeout. The cause was in the experiments file where epochs were started with:

```cpp
host->start_new_epoch(1.0, 0);
```

The first argument is the epoch start time in seconds. The value `1.0` means epoch 0 starts at t=1 s — coinciding exactly with the simulator's 1-second flow timeout. Every flow arrived at t=0, waited for matching that started at t=1 s, and timed out before any epoch ever fired.

The fix is a one-character change to the start time. We left the original line as a comment for non-all-to-all workloads:

```cpp
if(params.flow_type == PIM_FLOW) {
    for(int i = 0; i < params.num_hosts; i++) {
        PimHost* host = dynamic_cast<PimHost*>(topology->hosts[i]);
        if (host == NULL) continue;
        // for all-to-all
        host->start_new_epoch(0.0, 0);
        // for default
        // host->start_new_epoch(1.0, 0);
    }
}
```

#### Challenge 4: dcPIM — CCT independent of flow size

After fixing epoch initialization, all 16,256 flows completed, but CCT was about the same (~2500 µs) regardless of flow size. A 174-packet flow (2× BDP) finished in roughly the same time as a 43-packet flow (0.5× BDP). The paper shows these should differ by ~3×.

The cause is dcPIM's token-based pacing. After matching, the receiver sends a token to the sender for each data packet. The sender replies with one data packet per token. The check at the receiver is:

```cpp
if (this->token_gap() <= params.token_window) {
    schedule_token_proc_evt(0, false);  // send next token
}
```

where `token_gap() = token_count - largest_token_seq_received - 1`. The variable `token_window` therefore controls how many unacknowledged tokens (= data packets in flight) are allowed at once. The intended pacing is: the receiver waits for a data packet to arrive (a real round trip through congested switches) before sending the next token, which creates queueing-dependent back-pressure that scales with flow size.

In `params.cpp` the codebase contained:

```cpp
params.token_window *= params.BDP;
```

At 800 Gbps with 9 KB jumbo frames and the 7.98 µs RTT (including switch processing latency), BDP evaluates to 89 packets. So `token_window` was being scaled to `1 × 89 = 89`. With 89 packets allowed in flight, 89 tokens fire at once and the sender transmits 89 packets back-to-back. At 800 Gbps, 89 × 9 KB transmits in ~0.080 µs — essentially instant. A 174-packet flow drains in just 2 token batches; a 43-packet flow drains in 1. Both finish in roughly the same time and matching overhead dominates the CCT.

In the original dcPIM repo, `token_window: 1` is used at 100 Gbps without BDP scaling. At 100 Gbps, BDP is small (~5 packets), so even with scaling the window stays small. At 800 Gbps BDP is much larger and the window stops acting as a pacing mechanism.

**Fix:** comment out `params.token_window *= params.BDP` in `params.cpp` and set `token_window` directly in the config. We picked 6 because it is just above `pim_k = 4` — the number of matched links per host. With at most 6 packets in flight, the sender must wait for real per-packet acknowledgments before transmitting more, so queueing delay at switches actually accumulates and CCT scales correctly with flow size. A 174-packet flow needs ~30 token round-trips; a 43-packet flow needs ~8.

#### Challenge 5: Original baseline configs designed for 10–100 Gbps

After this debugging we cloned the original dcPIM repository (`github.com/Terabit-Ethernet/dcPIM`) and read `simulator/py/fat_tree/fat_tree.py`. The configurations used there are:

| Protocol | Original bandwidth |
|---|---|
| pHost (`flow_type: 112`, `host_type: 12`) | 10 Gbps |
| RUF (`flow_type: 115`, `host_type: 15`) | 10 Gbps |
| dcPIM (`flow_type: 116`, `host_type: 17`) | 100 Gbps |
| pFabric | 100 Gbps |

The dcSim paper runs everything at **800 Gbps** but the artifact does not include re-tuned baseline configurations. This explains the scaling bugs we found: dcPIM's `token_window` scaling that breaks at 800 Gbps; RUF's parameters tuned for 10 Gbps that cause it to crash at 800 Gbps; pFabric's congestion-window tuning that produces unrealistic results at 800 Gbps. The original configs are sensible at their target link speeds — they were just never re-tuned for the speed used in the paper.

#### Challenge 6: No documentation, no engineering diagrams

The codebase has no inline documentation explaining what parameters do, no architecture diagrams to show how components interact, and no comments describing expected parameter ranges. The scaling block in `params.cpp` for `PIM_HOST` contains commented-out lines with no explanation:

```cpp
// params.pim_epoch *= params.BDP * params.get_full_pkt_tran_delay();
// params.token_window *= params.BDP;
```

Working out what each parameter does meant reading the C++ implementation of the matching protocol, the token mechanism, and the epoch chaining logic in full, and then cross-referencing with the dcPIM paper. Parameters like `token_window`, `pim_link_pkts`, and `pim_iter_epoch` interact in non-obvious ways that are not described anywhere in the artifact.

#### Challenge 7: pHost RUF non-functional at 800 Gbps — capability parameters tuned by hand

The standalone RUF implementation (`flow_type: 115`) produces massive packet drops at t=0 under all-to-all load at 800 Gbps. All 16,256 RTS packets flood the network at the same time and the simulation either hangs or produces an empty FCT file.

We substituted `flow_type: 112` (capability transport) as pHost, which the original dcPIM repo also uses for its pHost comparisons. But no documented configuration exists for the capability transport at 800 Gbps. The paper publishes no pHost configuration at all. We tuned `capability_window`, `capability_timeout`, `capability_resend_timeout`, and `capability_window_timeout` entirely by hand. There is no guarantee these values match the authors' configuration. Our pHost reproduction is therefore the weakest of the three protocols — it passes the qualitative test (CCT scales with flow size, sits between dcSim and dcPIM) but cannot be validated quantitatively without the authors releasing their config.

#### Challenge 8: pFabric results unrealistically low

pFabric (`flow_type: 2`, `queue_type: 2`, `preemptive_queue: 1`) gives CCT ~3500 µs at 2× BDP, far below the paper's ~10,000 µs. Its retransmission and congestion window parameters are tuned for 100 Gbps in the original repo. At 800 Gbps these parameters cause flows to complete without correctly experiencing congestion. We could not resolve this and have omitted pFabric from our figure.

### 4.3 Results

**Paper's Figure 5(b):**

![Paper's Figure 5(b)](paper_fig5b.png)

**Our reproduction of Figure 5(b): dcSim vs dcPIM vs pHost all-to-all CCT:**

![Our reproduction of Figure 5(b)](reproduced_fig5b.png)

**Quantitative comparison:**

| Protocol | Flow size | Paper (µs) | Ours (µs) | Difference |
|---|---|---|---|---|
| dcSim | 0.5× | ~1800 | ~1600 | −11% |
| dcSim | 1×   | ~3500 | ~3100 | −11% |
| dcSim | 2×   | ~7500 | ~6100 | −19% |
| dcPIM | 0.5× | ~2500 | ~2200 | −12% |
| dcPIM | 1×   | ~4500 | ~4400 | −2%  |
| dcPIM | 2×   | ~8000 | ~8600 | +8%  |
| pHost | 0.5× | ~2500 | ~3000 | +20% |
| pHost | 1×   | ~4500 | ~4400 | −2%  |
| pHost | 2×   | ~7500 | ~7200 | −4%  |

All the key qualitative claims from the paper are confirmed:

- dcSim achieves the lowest CCT at all flow sizes 
- All three protocols show CCT increasing with flow size 
- dcPIM and pHost perform similarly, both slower than dcSim 
- The gap between dcSim and baselines grows with flow size 

**Zero loss and zero reordering observed for dcSim.** Across all flow sizes, dcSim produced zero data packet drops and near-zero reordering, which directly confirms the paper's main lossless claim. The numbers below come from the final line of the `drop_reorder` log for each dcSim run:

| Flow size | Total data packets | Drops | Reordered packets |
|---|---|---|---|
| 0.5× BDP  | 698,984     | 0 | 4 |
| 1× BDP    | 1,414,271   | 0 | 1 |
| 2× BDP    | 2,828,525   | 0 | 0 |

Quantitative CCT differences from the paper range from 2% to 20%. The remaining gap is due to: (1) single run vs. the paper's 20-run average; (2) `token_window: 6` for dcPIM was found empirically; (3) the 450 ns switch delay needed manual investigation; (4) pHost parameters were tuned by hand because no published configuration exists for 800 Gbps. Our pHost numbers should be read as a best-effort approximation rather than a faithful reproduction of the authors' run.

### 4.4 Reproducibility Assessment

**dcSim** is the only protocol that reproduces reliably. Once the switch processing latency (450 ns) was identified and applied via Theorem 2 in `get_worst_rtt()`, CCT matched the paper within 10–20%, and the lossless / zero-reorder claims were directly confirmed by the output logs.

**dcPIM** required two fixes that took several weeks to identify: the epoch initialization bug (Challenge 3) and the token window scaling bug (Challenge 4). Neither issue appears in the paper or artifact documentation.

**pHost** uses `flow_type: 112` (capability transport) — confirmed from the original dcPIM repo. The native RUF implementation is non-functional at 800 Gbps. Parameters were found empirically with no published reference; results are a best-effort approximation only.

**pFabric** could not be reproduced. Results are roughly 3× too low and we could not find a clear explanation.

**Overall:** the artifact is sufficient to reproduce dcSim itself but not sufficient to reproduce the baseline comparisons without substantial reverse-engineering. A researcher without deep familiarity with the dcPIM codebase would not be able to verify that dcSim genuinely outperforms the baselines.

---

## 5. Further Exploration — dcSim vs dcPIM on Non-AI (Web-Search) Traffic

### 5.1 Motivation

dcSim was designed and evaluated **exclusively for AI-training collectives** — synchronized, large, all-to-all and permutation transfers. Its core mechanism is the Sim probe: before every Data packet, a 64-byte probe is sent along the same path, and Data follows only after a fixed delay `RTT_max`. This adds a *fixed per-flow* latency. We wanted to know whether this overhead still pays off under **general (non-AI) datacenter traffic**, where flows are smaller and far more varied in size.

We tested this with a classic **web-search workload** and compared dcSim against **dcPIM**, the paper's main baseline. dcPIM is a *proactive* transport: long flows are matched to receivers through a constant number of request/grant/accept rounds *before* sending, while **short flows are sent immediately without matching**.

### 5.2 The Web-Search Workload

The web-search workload models traffic from a search engine cluster (e.g. a Google-style query/response pipeline). It is one of the standard datacenter benchmarks introduced in the DCTCP paper (Alizadeh et al., SIGCOMM 2010) and reused widely since. We used the standard `websearch.cdf` distribution provided with the dcPIM simulator:

```
size_in_pkts   CDF_prob   weight
1              0.15       0.15
2              0.20       0.20
4              0.30       0.30
6              0.40       0.40
15             0.50       0.50
75             0.60       0.60
149            0.70       0.70
373            0.80       0.80
746            0.90       0.90
2238           1.00       1.00
```

How to read this: each row gives a flow size in packets and the cumulative probability that a randomly chosen flow has at most that size. So 15% of flows are exactly 1 packet (a single MTU = 9 KB at jumbo size), 30% of flows are at most 4 packets, 50% are at most 15 packets, and so on. The largest possible flow is 2238 packets (~20 MB at 9 KB MTU).

This distribution is **heavy-tailed**: the majority of flows are very small (50% of flows are ≤ 15 packets, i.e. ≤ 1× BDP at our 800 Gbps setting where BDP = 87 packets), but the few large flows in the tail carry most of the actual bytes. This is the opposite of the all-to-all collective we used for Figure 5(b), where all 16,256 flows have the same size and start at the same time.

The two workloads stress the network in fundamentally different ways:

| Property | All-to-all (Section 4) | Web-search (this section) |
|---|---|---|
| Number of flows | Fixed: 16,256 | Varied: 2000 in our runs |
| Flow size | Fixed (43 / 87 / 174 pkts) | Variable, from 1 to 2238 pkts |
| Arrival pattern | All start at t=0 (synchronous burst) | Poisson process (open loop) |
| Source-destination pairs | All-to-all (every host pairs with every other) | Random pairs |
| Network state | Saturated congestion | Lightly loaded core, bursty arrivals |
| Performance metric | CCT (collective completion time) | FCT (per-flow completion time) |

For web-search there is no single CCT to measure because flows are independent — there is no collective. The interesting metric is per-flow FCT, looked at through three lenses: **median** (the typical small flow), **average** (pulled up by the large tail), and **p99** (the slowest 1%, the metric datacenters care about most).

### 5.3 Offered Load

The arrival rate is a Poisson process tuned so the average offered traffic matches a target *load* — a fraction of the network capacity. **Load 0.4** means the senders inject traffic at 40% of access link capacity on average. **Load 0.6** is 60%, **load 0.8** is 80%. Higher load means more flows per unit time, more contention at receivers, more queueing.

Concretely: at load 0.4 with our 2000-flow target, the simulator generates flow arrivals at a rate calibrated so that, integrated over time, the senders push out 40% of the access link capacity in average bytes. The flow size distribution itself (the CDF above) does not change with load — only the arrival rate does. We swept loads 0.4, 0.6, 0.8 because these are the values used by the dcPIM and pHost papers; above 0.8 the network is approaching instability and FCT measurements become unreliable.

### 5.4 Setup

Same network as our reproduction (Fat-Tree k=8, 128 hosts, 800 Gbps, 9 KB jumbo frames). Both algorithms run on *identical* traffic — same web-search flow-size CDF, same loads, same `num_flow = 2000`, and the same RNG seed (`srand(0)`); only the transport differs (`flow_type` 117 = dcSim, 116 = dcPIM). We swept offered load **0.4 / 0.6 / 0.8** and report per-flow FCT as **median, average, and p99**.

**Expectation:** since the Sim probe is a fixed per-flow cost, we expected it to be relatively expensive for the many small web-search flows.

### 5.5 First (Incorrect) Comparison

Our first run gave a surprising result: dcSim appeared to *beat* dcPIM on the median flow (~25 µs vs. ~40 µs), with dcPIM only slightly ahead on average and tail. This contradicts dcPIM's design (it is supposed to be fast on short flows), so we treated it as suspicious rather than conclusive.

![Figure 5.1 — First comparison (incorrect): with the short-flow threshold unset, dcPIM's fast path is disabled, so its median FCT is inflated and dcSim appears to win.](non-ai-1.jpg)

### 5.6 Diagnosis and Fix

Inspecting dcPIM's code and paper, we found that its short-flow threshold is the parameter `token_initial`: a flow with `size_in_pkt ≤ token_initial × BDP` is sent immediately, otherwise it must wait for matching. Our configuration never set `token_initial`, so it defaulted to 0 — every flow (even single-packet ones) was forced through the matching phase, which disabled dcPIM's short-flow fast path.

The dcPIM paper uses "1 BDP as short flow size threshold" (§4.1). We therefore set `token_initial: 1` (→ 1 × BDP = 87 packets), restoring the paper's behaviour — a one-line config change, no recompilation needed.

### 5.7 Corrected Comparison

After the fix, dcPIM's median FCT dropped from ~40 µs to ~6 µs (small flows now transmit immediately, ~1 RTT), and **dcPIM beats dcSim on all three metrics**:

| Metric (µs) | dcSim (0.4 / 0.6 / 0.8) | dcPIM (0.4 / 0.6 / 0.8) |
|---|---|---|
| Median  | 21 / 25 / 25     | 6 / 6 / 7       |
| Average | 88 / 111 / 127   | 72 / 92 / 106   |
| p99     | 750 / 947 / 1034 | 672 / 815 / 921 |

![Figure 5.2 — Corrected comparison (`token_initial = 1 BDP`): dcPIM sends small flows immediately and is faster than dcSim on median, average, and p99 at every load.](non-ai-2.jpg)

### 5.8 Interpretation

We read FCT through three lenses because web-search is heavy-tailed: the **median** is the typical (small) flow, the **average** is pulled up by the few very large flows, and **p99** is the tail (slowest 1%, the metric datacenters care about most). dcPIM is faster on all three, but its advantage is **largest on the median (~4×)** and shrinks on average and tail (~10–18%).

The reason is the nature of dcSim's overhead. The Sim probe adds a *fixed* delay (~`RTT_max`) before every flow. For a tiny flow that would otherwise finish in ~6 µs this is a huge relative cost (~4×); for a large flow it is a small fraction of the transfer and amortizes away. Web-search is **dominated by small flows** — exactly the regime where dcSim's fixed probe cost hurts most, while dcPIM sends those flows immediately. This is consistent with the dcSim paper's own observation that it loses to dcPIM on short flows below 1 BDP (Figure 5, 0.5× BDP).

### 5.9 Can dcSim Be Saved by Adapting `RTT_max`?

The fixed `RTT_max = 22 µs` used by dcSim is a worst-case bound from Theorem 2: it covers the maximum possible queueing across the path even under heavy load. Under lighter load the actual Sim-Ack RTTs are much smaller. We asked: what if dcSim adapts `RTT_max` based on observed Sim-Ack RTTs? Could the gap with dcPIM be closed by using a more realistic wait time?

We tested two variants:

- **Adaptive SNS** — Data is scheduled at `tSim + ewma_rtt`, where `ewma_rtt` is updated on every Sim-Ack as `0.875 × ewma + 0.125 × measured`. We initialised `ewma_rtt` to `params.rtt = 7.8 µs` (the zero-load RTT) so the first Data is not scheduled too early before the EWMA has any observations. The only code change is in `ext/snsflow.cpp`:

```cpp
this->current_avg_rtt =
    0.875 * this->current_avg_rtt + 0.125 * measured_rtt;
// ...
time_to_be_sent = this->sim_window[slot_id].time + this->current_avg_rtt;
```

- **Async SNS** — `synced_mode: 0`, which already exists in the codebase. Data is sent immediately when the Sim-Ack arrives, with no additional waiting. This is essentially `ewma_rtt = measured_rtt` exactly, per Sim, with no smoothing.

We ran the same web-search workload as in Section 5.7 with these two variants alongside fixed dcSim and dcPIM.

### 5.10 Adaptive vs. Async Results

![Figure 5.3 — Web-search FCT comparison across four protocols: fixed dcSim, dcPIM, adaptive SNS, async SNS](websearch_fct_4protocols.png)

| Metric | Load | dcSim | dcPIM | Adaptive SNS | Async SNS |
|---|---|---|---|---|---|
| Median (µs)  | 0.4 | 45 | 6   | 25  | 23  |
| Median (µs)  | 0.6 | 46 | 7   | 28  | 25  |
| Median (µs)  | 0.8 | 46 | 7   | 27  | 26  |
| Average (µs) | 0.4 | 190 | 71 | 165 | 161 |
| Average (µs) | 0.6 | 222 | 90 | 197 | 195 |
| Average (µs) | 0.8 | 241 | 103| 216 | 213 |
| p99 (µs)     | 0.4 | 1580 | 670 | 1500 | 1500 |
| p99 (µs)     | 0.6 | 1730 | 835 | 1620 | 1650 |
| p99 (µs)     | 0.8 | 1750 | 935 | 1740 | 1740 |

Three things stand out:

1. **Adaptive SNS and async SNS are almost identical** on average and p99 across all three loads. Adaptive is only slightly worse than async on median (by 2–3 µs).
2. **Both variants beat fixed dcSim** on median (by ~20 µs) but they are still **2–4× slower than dcPIM on median**.
3. The gap between the dcSim variants and dcPIM shrinks at higher loads as the network's actual RTT grows, but dcPIM stays ahead at all loads.

### 5.11 Why Adaptive ≈ Async

To explain why adaptive SNS and async SNS behave almost the same, we logged both the EWMA RTT (used by adaptive) and the measured Sim-Ack RTT (used by async) on every Sim-Ack.

![Figure 5.4 — RTT analysis showing EWMA converges to actual measured RTT](rtt_analysis.png)

| Load | Adaptive (median EWMA RTT) | Async (median measured RTT) | Gap | Fixed RTT_max |
|---|---|---|---|---|
| 0.4 | 3.75 µs | 3.72 µs | +0.03 µs | 22.0 µs |
| 0.6 | 3.95 µs | 3.97 µs | −0.02 µs | 22.0 µs |
| 0.8 | 4.08 µs | 4.12 µs | −0.03 µs | 22.0 µs |

The gap between the EWMA RTT used by adaptive SNS and the measured RTT used by async SNS is essentially zero (±0.03 µs across all loads). The EWMA has converged to the actual observed RTT, so both protocols schedule Data at approximately the same time and produce essentially the same FCT.

Two further observations explain the rest:

**(a) Actual Sim-Ack RTTs under web-search load are far below `RTT_max`.** The measured median is ~4 µs across all three loads — well below `RTT_max = 22 µs` and even below the zero-load RTT of 7.8 µs. Web-search flows are short, so the network core is lightly loaded; Sim-Acks rarely encounter deep queueing. Fixed dcSim therefore waits ~18 µs longer than necessary per flow, which directly explains why its median FCT is so much higher.

**(b) Adaptive SNS only helps on flows long enough to warm up the EWMA.** A flow that only sends 1–2 Sims completes before the EWMA has moved much from its initial 7.8 µs, so adaptive SNS gives little benefit for the very smallest flows. Async SNS uses the measured RTT directly and so benefits even the first Sim of every flow. This explains the small median gap (~2–3 µs) between async and adaptive, despite the convergence on average and p99.

### 5.12 Conclusion

Adaptive `RTT_max` does improve dcSim's FCT on web-search Poisson traffic compared to the fixed-`RTT_max` baseline. But it does not close the gap with dcPIM. dcPIM achieves ~6 µs median FCT because it sends small flows immediately, with no probe at all. The fundamental cost of dcSim on web-search traffic is the *existence* of the Sim probe itself, not its parameterisation. Any dcSim variant — fixed, adaptive, or async — still pays one probe RTT per flow, which dominates the completion time for small flows. dcPIM bypasses this entirely for flows below 1 BDP via its `token_initial` short-flow path.

So on general (non-AI) web-search traffic, **dcSim's Sim-probe overhead is not worth it**: dcPIM achieves lower FCT across median, average, and tail, with the largest gap on the typical small flow. dcSim's design is tuned for large AI-training collectives, where the one-time probe amortizes over many BDP of data; on small flows it is a fixed tax that dcPIM avoids entirely by sending short flows immediately.

---

## 6. Reproducibility Assessment of the Paper

The paper is well-written and scientifically rigorous. The methodology is clearly described, the claims are precise, and the evaluation covers a comprehensive range of scenarios. However, the artifact does not match the level of quality of the paper itself.

**Missing switch delay documentation.** The paper mentions 450 ns switch processing latency and a zero-load RTT of 7.8 µs. Neither the README nor any config file explains how to set this in the simulator. Without it, dcSim results are 3× too fast. A researcher reproducing this work must read the paper carefully, locate the parameter in the C++ source, and discover that `params.rtt` already hardcodes a `+ 0.0000045` term that `get_worst_rtt()` was bypassing.

**No trace generation scripts.** The paper evaluates all-to-all and permutation collectives but provides no scripts to generate the trace files these require. The trace format is not documented; it must be reverse-engineered from the `FlowReader` C++ class.

**Baseline protocols non-functional at 800 Gbps.** The artifact ships code for dcPIM, pHost, pFabric, and RUF alongside dcSim, but these baseline implementations were designed for 10–100 Gbps and have never been re-tuned for 800 Gbps. Two code bugs in dcPIM required weeks of debugging. pHost (RUF) is completely non-functional at 800 Gbps; we substituted the capability transport from the same codebase as the closest available analog. pFabric produces unrealistically low CCT. The paper reports results for all four protocols at 800 Gbps but does not publish the configurations used to produce those results.

**No documentation or engineering diagrams for dcPIM and other baselines.** The dcPIM section of the codebase has no README, no comments explaining parameter meanings, no architecture diagrams, and no example configs. Critical parameters like `token_window`, `pim_iter_epoch`, `token_initial`, and the scaling block in `params.cpp` require reading the C++ implementation of the matching protocol and the token mechanism end-to-end to tune correctly. Several commented-out lines in the scaling block have no explanation of why they are commented out or what values should be used instead.

In summary: **dcSim itself is reproducible given enough effort.** The baselines are not reproducible from the artifact alone, which significantly undermines the paper's comparative claims. A researcher without deep familiarity with the dcPIM codebase would not be able to verify that dcSim genuinely outperforms the baselines, since the baselines cannot be run correctly at the paper's chosen link speed.

---

## 7. Conclusion

We successfully reproduced Figure 5(b) of the dcSim paper — the CCT comparison for single all-to-all collectives at 0.5×, 1×, and 2× BDP — confirming the paper's key claim that dcSim achieves lower CCT than dcPIM and pHost at all reproduced flow sizes. Our quantitative results match the paper within 2–20%. We also directly verified the paper's lossless and reorder-free claim: dcSim produced zero data packet drops and near-zero reordering across all three flow sizes.

However, reproduction was significantly harder than expected. Reproducing dcSim itself required identifying an undocumented switch processing latency parameter and implementing Theorem 2 directly to bypass a broken `get_worst_rtt()` function. Reproducing dcPIM required fixing two code-level bugs that took several weeks to diagnose. pHost required substituting an alternative implementation, and even then its parameters had to be found entirely by hand. pFabric could not be reproduced at all.

Our novel experiment (Section 5) shows that on general web-search traffic, dcPIM outperforms dcSim across all metrics, with the largest advantage on small flows where dcSim's fixed probe overhead is most expensive. We also showed that adapting `RTT_max` based on observed Sim-Ack RTTs (via EWMA or per-packet async scheduling) does improve dcSim relative to its fixed-`RTT_max` baseline, but it cannot close the gap with dcPIM. The Sim probe itself is the per-flow bottleneck for small flows, not its delay value. This confirms that dcSim's design is specifically optimized for large AI training collectives where the per-flow probe cost amortizes, and should not be expected to generalize directly to small-flow workloads.

---

## Appendix A: dcPIM Protocol Summary

dcPIM organizes time into epochs. Each epoch has two phases:

**Matching phase** (`pim_iter_limit` iterations of REQ/Grant/Accept):

- Receivers send REQ packets to their preferred senders
- Senders grant their preferred receivers
- Receivers confirm with Accept
- After `pim_iter_limit` iterations, matched pairs are established

**Data phase** (token-based transfer):

- Receiver sends one token to sender per data packet it is ready to receive
- Sender receives token, sends one data packet
- `token_window` controls how many unacknowledged tokens (= packets in flight) are allowed simultaneously
- CCT scales correctly with flow size only when `token_window` creates genuine queueing pressure

A flow with `size_in_pkt ≤ token_initial × BDP` is sent immediately without going through matching. For workloads dominated by small flows (e.g. web-search), setting `token_initial: 1` enables this short-flow fast path; for all-to-all with uniform flow sizes, `token_initial: 0` forces every flow through matching.

---

## Appendix B: Why Capability Transport Was Used for pHost

Confirmed from the original dcPIM repository (`simulator/py/fat_tree/fat_tree.py`, `conf_str_phost`): pHost in that codebase uses `flow_type: 112`, `host_type: 12` at 10 Gbps. This is the capability-based transport — receivers issue capability tokens to senders, who can only send when holding a valid token, with `capability_window` controlling pacing. This matches pHost's core mechanism as described in the pHost paper (Gao et al., CoNEXT 2015).

The separate RUF implementation (`flow_type: 115`, `host_type: 15`) in the codebase is also designed for 10 Gbps and is non-functional at 800 Gbps under all-to-all load.

---

## Appendix C: Code Changes Made to dcPIM and dcSim

**Change 1: Epoch start time fixed in the experiments file.**

The original code starts epochs at t=1 s, coinciding exactly with the simulator's 1-second flow timeout. We changed the start time to 0.0 for all-to-all workloads:

```cpp
if(params.flow_type == PIM_FLOW) {
    for(int i = 0; i < params.num_hosts; i++) {
        PimHost* host = dynamic_cast<PimHost*>(topology->hosts[i]);
        if (host == NULL) continue;
        // for all-to-all
        host->start_new_epoch(0.0, 0);
        // for default
        // host->start_new_epoch(1.0, 0);
    }
}
```

**Change 2: Token window BDP scaling commented.**

```cpp
// params.token_window *= params.BDP;   // commented out
// token_window is now set directly from the config (token_window: 6)
```

**Change 3: `get_worst_rtt()` rewritten to implement Theorem 2 directly.**

The original function bypassed `params.rtt` (which already includes the 4.5 µs switch processing term) and recomputed RTT from scratch without the switch term. The new implementation uses `params.rtt` as the base and adds the queueing term from Theorem 2:

```cpp
double alpha = params.mss / (double)params.hdr_size;
double queueing_term =
    (2.0 * num_hops * params.hdr_size * 8.0 / params.bandwidth)
    * ((params.simulation_queue_size + 1) * alpha
       + 2.1 * params.simulation_queue_size + 1);
total = params.rtt + queueing_term;  // ≈ 7.8 µs + 14.2 µs = 22 µs
```

**Change 4: Adaptive `RTT_max` in `ext/snsflow.cpp` (Section 5.9 experiment).**

On every Sim-Ack receipt, update the EWMA and use it in place of `params.worst_rtt`:

```cpp
double measured_rtt = get_current_time() - this->sim_window[slot_id].time;
this->current_avg_rtt =
    0.875 * this->current_avg_rtt + 0.125 * measured_rtt;
// ...
time_to_be_sent = this->sim_window[slot_id].time + this->current_avg_rtt;
```

`current_avg_rtt` is initialised to `params.rtt` (7.8 µs) so the first Data is not scheduled too early.


