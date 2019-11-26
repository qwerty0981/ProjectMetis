import sys
import json
import networkx as nx
import matplotlib.pyplot as plt

color = [
    [[0.0, 0.0, 0.0]],
    [[0.1, 0.1, 0.1]],
    [[0.2, 0.2, 0.2]],
    [[0.3, 0.3, 0.3]],
    [[0.4, 0.4, 0.4]],
    [[0.5, 0.5, 0.5]],
    [[0.6, 0.6, 0.6]],
    [[0.7, 0.7, 0.7]],
    [[0.8, 0.8, 0.8]],
    [[0.9, 0.9, 0.9]],
    [[1.0, 1.0, 1.0]]
]

if len(sys.argv) != 3:
    print("Missing file name to visualize! Ex. python visualize.py <model> <sim output>")
    exit(1)

simFile = open(sys.argv[2], 'r')

neurons = []

for line in simFile:
    vals = [v.split(":") for v in line.strip().split("\t")]
    n = dict()
    for v in vals:
        n[v[0]] = v[1]

    neurons.append(n)

simFile.close()

for neuron in neurons:
    if int(neuron["Activity Level"]) < 0:
        neuron["Activity Level"] = 0

model = {}

with open(sys.argv[1], 'r') as modelFile:
    model = json.load(modelFile)

mNeurons = model["neurons"]

G = nx.Graph()

for neuron in mNeurons:
    G.add_node(neuron["name"])

for neuron in mNeurons:
    for con in neuron["connections"]:
        G.add_edge(neuron["name"], con["neuron"])

pos = nx.spring_layout(G)

for t in range(model["simulationLength"]):
    for i in range(11):
        nodes = ["neuron" + n["Neuron"] for n in neurons if int(n["Activity Level"]) == i and int(n["Time"]) == t]
        nx.draw_networkx_nodes(G, pos, nodelist=nodes, node_color=color[i], alpha = 0.8)

    nx.draw_networkx_edges(G, pos)
    plt.savefig("fig_" + str(t) + ".png")
    plt.clf()
    


# nx.draw(G)

# print(model)