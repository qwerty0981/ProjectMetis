from random import randint, sample
import json
import networkx as nx
import matplotlib.pyplot as plt
import sys

numberOfNeuronsToGenerate = 300
averageNumberOfConnectionsPerNeuron = 2
simulationLength = 30
numberOfStimulus = 4
numberOfConnectionsPerStimulus = 5

# ======= Neurons Start =======
neurons = []

# First Step: Initialize the list of neurons
for i in range(numberOfNeuronsToGenerate):
    neuron = dict()
    neuron["name"] = "neuron" + str(i)
    neuron["connections"] = []
    neurons.append(neuron)

# Second Step: Create connections between the neurons
for id, neuron in enumerate(neurons):
    connectionIds = []
    for i in range(averageNumberOfConnectionsPerNeuron + randint(-1, 1)):
        rid = id
        while rid == id and rid not in connectionIds:
            rid = randint(0, numberOfNeuronsToGenerate-1)
        connectionIds.append(rid)
    
    connections = [{ "neuron": neurons[zid]["name"], "sensitivity": randint(1, 10)/10 } for zid in connectionIds]
    neuron["connections"] = connections


# for neuron in neurons:
#     print(neuron["name"])
#     print(neuron["connections"])

# print(neurons)

# ======= IO Start =======
io = []

for i in range(numberOfStimulus):
    stim = dict()
    stim["name"] = "Stimulus " + str(i)
    stim["type"] = 0
    stim["connections"] = [{"neuron": n["name"]} for n in sample(neurons, numberOfConnectionsPerStimulus)]
    stim["offset"] = 2 * i + randint(0, 3)
    stim["duration"] = 2
    stim["amplitude"] = 20
    io.append(stim)

config = {
    "neurons": neurons,
    "io": io,
    "simulationLength": simulationLength
}

print(json.dumps(config))

if len(sys.argv) > 1 and sys.argv[1] == "show":
    G = nx.Graph()

    for neuron in neurons:
        G.add_node(neuron["name"])

    for neuron in neurons:
        for con in neuron["connections"]:
            G.add_edge(neuron["name"], con["neuron"])

    nx.draw(G)
    plt.show()