import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

runs = [
     ("output/bdp0.5/fct.txt", "0.5x"),
    ("output/bdp1/fct.txt", "1x"),
    ("output/bdp2/fct.txt",   "2x"),
]

labels = []
ccts = []  # Collective Completion Time = max FCT

for path, label in runs:
    fcts = []
    with open(path) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                fcts.append(float(parts[1]))
    labels.append(label)
    ccts.append(max(fcts))  # last flow to finish

x = np.arange(len(labels))
plt.figure(figsize=(8, 5))
plt.bar(x, ccts, width=0.4, color='red', hatch='..', label='dcSim')
plt.xticks(x, labels)
plt.xlabel("Flow Size (multiple of BDP)")
plt.ylabel("Collective Completion Time (µs)")
plt.title("dcSim - Collective Completion Time vs Flow Size")
plt.legend()
plt.grid(axis='y')
plt.tight_layout()
plt.savefig("output/cct_comparison.png")
print("Saved to output/cct_comparison.png")