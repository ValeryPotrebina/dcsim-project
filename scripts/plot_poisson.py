import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

loads = [0.4, 0.6, 0.8]
paths = {
    0.4: "output/poisson_dcsim/load0.4/fct.txt",
    0.6: "output/poisson_dcsim/load0.6/fct.txt",
    0.8: "output/poisson_dcsim/load0.8/fct.txt",
}

avg, p50, p99 = [], [], []
for L in loads:
    fcts = []
    with open(paths[L]) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                fcts.append(float(parts[1]))
    fcts = np.array(fcts)
    avg.append(fcts.mean())
    p50.append(np.percentile(fcts, 50))
    p99.append(np.percentile(fcts, 99))

plt.figure(figsize=(8, 5))
plt.plot(loads, p99, 's--', color='darkred', label='dcSim p99 FCT')
plt.plot(loads, avg, 'o-', color='red', label='dcSim avg FCT')
plt.plot(loads, p50, '^:', color='salmon', label='dcSim median FCT')

for x, y in zip(loads, p99):
    plt.annotate(f"{y:.0f}", (x, y), textcoords="offset points", xytext=(0, 8), ha='center')
for x, y in zip(loads, avg):
    plt.annotate(f"{y:.0f}", (x, y), textcoords="offset points", xytext=(0, 8), ha='center')

plt.xlabel("Load")
plt.ylabel("Flow Completion Time (us)")
plt.title("dcSim - Web-search FCT vs Load (N=2000 flows)")
plt.xticks(loads)
plt.legend()
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig("output/poisson_dcsim/fct_vs_load.png")
print("Saved to output/poisson_dcsim/fct_vs_load.png")
