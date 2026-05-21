import random

def generate_permutation(N, flow_size_pkts, output_file, collective_id=2):
    flow_id = 0
    with open(output_file, "w") as f:
        for src in range(N):
            dst = (src + 1) % N  # each host sends to next host in ring
            f.write(f"{flow_id} 0.0 {collective_id} 0 "
                    f"{flow_size_pkts} 0 0 {src} {dst}\n")
            flow_id += 1

def generate_all_to_all(N, flow_size_pkts, output_file, collective_id=2):
    flow_id = 0
    with open(output_file, "w") as f:
        for src in range(N):
            for dst in range(N):
                if src != dst:
                    f.write(f"{flow_id} 0.0 {collective_id} 0 "
                            f"{flow_size_pkts} 0 0 {src} {dst}\n")
                    flow_id += 1

def generate_all_to_all_v(N, flow_size_pkts, output_file, collective_id=2):
    flow_id = 0
    with open(output_file, "w") as f:
        for src in range(N):
            for _ in range(N - 1):
                dst = random.choice([h for h in range(N) if h != src])
                f.write(f"{flow_id} 0.0 {collective_id} 0 "
                        f"{flow_size_pkts} 0 0 {src} {dst}\n")
                flow_id += 1

# Paper uses N=128, flow sizes as multiples of BDP where 1 BDP = 87 packets
generate_all_to_all(128, 87, "all_to_all_1bdp.tr")
generate_all_to_all(128, 174, "all_to_all_2bdp.tr")  # 2x BDP
generate_all_to_all(128, 43, "all_to_all_0.5bdp.tr")
generate_all_to_all(128, 348, "all_to_all_4bdp.tr")

