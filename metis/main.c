#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "cJSON.h"

#define METIS_MAX_NUERON_NAME 20
#define METIS_MAX_IO_NAME 20
#define METIX_MAX_IO_OUTPUT_PREFIX 20

#define MASTER 0

#define DEBUG 0
#define OUTPUT_STATE 1

// Message Types
#define METIS_DATA_REQUEST		1
#define METIS_TASK				2
#define METIS_TIME_UPDATE		3
#define METIS_TASK_DONE			4
#define METIS_DATA_RESPONSE		5
#define METIS_CONFIG			6

const char DEFUALT_FILE[] = "model.json";

struct metisNeuron;
struct metisNeuronConnection;
struct metisIoConnection;
struct metisIO;
struct metisConfig;

typedef struct metisNeuronConnection {
	struct metisNeuron* neuron;
	double sensitivity;
	struct metisNeuronConnection* next;
} metisNeuronConnection;

typedef struct metisIoConnection {
	struct metisNeuron* neuron;
	struct metisIoConnection* next;
} metisIoConnection;

typedef struct metisNeuron {
	char name[METIS_MAX_NUERON_NAME];
	struct metisNeuronConnection* connections;
	int connectionsLength;
	int nextValue;
	int ownerId;
	int id;
	int activityLevel;
	struct metisNeuron* next;
} metisNeuron;

typedef struct metisIO {
	char name[METIS_MAX_IO_NAME];
	int type;								// 0 = stimulus, 1 = reader
	metisIoConnection* connections;
	struct metisIO* next;
	int connectionsLength;
	int offset;
	int duration;
	int amplitude;
	char outputPrefix[METIX_MAX_IO_OUTPUT_PREFIX];
} metisIO;

typedef struct metisConfig {
	metisNeuron* neurons;
	int neuronLength;
	metisIO* io;
	int ioLength;
	int simulationLength;
} metisConfig;


cJSON* parseFile(char*);
metisConfig* parseConfig(cJSON*);
void metisAddConnection(metisNeuronConnection*, metisNeuronConnection*);
void metisAddIoConnection(metisIoConnection*, metisIoConnection*);
metisNeuronConnection* metisNewNeuronConnection(metisNeuron*, double);
metisConfig* metisNewConfig();
metisIO* metisNewIO();
metisNeuron* metisNewNeuron();
metisIoConnection* metisNewIoConnection();
void metisFreeConfig(metisConfig*);
void metisFreeIO(metisIO*);
void metisFreeNeuron(metisNeuron*);
void metisAddNeuronConnection(metisNeuron*, metisNeuronConnection*);
void metisAddIOConnection(metisIO*, metisIoConnection*);
void metisConfigAddNeuron(metisConfig*, metisNeuron*);
void metisConfigAddIO(metisConfig*, metisIO*);
metisNeuron* metisGetNeuronByName(metisConfig*, char*);
void metisFreeIoConnections(metisIoConnection*);
void metisFreeNeuronConnections(metisNeuronConnection*);
void runMasterNode(metisConfig*, int);
void runWorkerNode(metisConfig*, int, int);
bool arrayContains(int*, int, int);

int main(int argc, char** argv) {
	// Initialize the MPI environment
	MPI_Init(NULL, NULL);

	// Get the number of processes
	int world_size;
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	// Get the rank of the process
	int world_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

	// Get the name of the processor
	char processor_name[MPI_MAX_PROCESSOR_NAME];
	int name_len;
	MPI_Get_processor_name(processor_name, &name_len);
	char* filename;

	if (argc > 1) {
		filename = argv[1];
	} else {
		filename = DEFUALT_FILE;
	}
	//const char filename[] = "model.json";

	cJSON* file = parseFile(filename);
	if (file == NULL) {
		fprintf(stderr, "Failed to parse file '%s'\n", filename);
		MPI_Finalize();
		return 1;
	}
	metisConfig* config = parseConfig(file);
	if (config == NULL) {
		MPI_Finalize();
		return 1;
	}

	// Check if the number of neurons is >= number of nodes
	if (config->neuronLength < world_size - 1) {
		if (world_rank == 0) {
			printf("There are more nodes then neurons!\n");
			printf("Exiting...\n");
		}
		MPI_Finalize();
		return 0;
	}

	if (world_rank == MASTER && DEBUG) {
		printf("Successfully read the config!\n");
		printf("Read %d neurons\n", config->neuronLength);
		metisNeuron* cursor = config->neurons;
		for (int i = 0; i < config->neuronLength; i++) {
			printf("Neuron index %d:\n\tName: %s\n", i, cursor->name);
			metisNeuronConnection* cur = cursor->connections;
			for (int j = 0; j < cursor->connectionsLength; j++) {
				printf("\t\tConnection Name: %s, sensitivity: %f\n", cur->neuron->name, cur->sensitivity);
				cur = cur->next;
			}
			cursor = cursor->next;
		}
		printf("Read %d io devices\n", config->ioLength);
		printf("Sim length: %d\n", config->simulationLength);
	}

	// Test printing from different nodes
	if (world_rank == 0) {
		// I am master
		runMasterNode(config, world_size);
	}
	else {
		// I am a worker node
		runWorkerNode(config, world_rank, world_size);
	}

	// Clean Up the memory used by our config object
	metisFreeConfig(config);

	if (world_rank == MASTER) {
		if (DEBUG)
			printf("Successfully freed all memory used by metis config object\n");
	}

	// Finalize the MPI environment.
	MPI_Finalize();

	return 0;
}

void runMasterNode(metisConfig* config, int numberOfNodes) {
	// Assign nodeIds to neurons
	metisNeuron* cursor = config->neurons;
	int nodeId = 1;
	for (int i = 0; i < config->neuronLength; i++) {
		if (DEBUG)
			printf("MASTER> Assigned neuron %d to node %d\n", i, nodeId);
		cursor->ownerId = nodeId;
		nodeId = (nodeId + 1) % numberOfNodes;
		if (nodeId == 0) {
			nodeId++;
		}
		cursor = cursor->next;
	}

	int maxNumberOfNeuronsPerNode = config->neuronLength / (numberOfNodes - 1);
	if (config->neuronLength % (numberOfNodes - 1) != 0) {
		maxNumberOfNeuronsPerNode++;
	}
	if (DEBUG)
		printf("MASTER> Max number of neurons per node: %d\n", maxNumberOfNeuronsPerNode);

	// Send neurons to assigned node
	cursor = config->neurons;
	for (int nodeId = 1; nodeId < numberOfNodes; nodeId++) {
		int nodes[maxNumberOfNeuronsPerNode];
		// Set the starting nodes to -1 to indicate they are not assigned
		memset(nodes, -1, maxNumberOfNeuronsPerNode);

		int nodeRef = 0;
		for (int i = 0; i < config->neuronLength; i++) {
			if (cursor->ownerId == nodeId) {
				// Add the node to the list
				nodes[nodeRef] = cursor->id;
				nodeRef++;
			}
			cursor = cursor->next;
		}
		// Reset the cursor
		cursor = config->neurons;

		// Send the list to the client
		MPI_Send(nodes, nodeRef, MPI_INT, nodeId, METIS_TASK, MPI_COMM_WORLD);
	}

	int nodePairs[config->neuronLength * 2];
	int i = 0;
	for (metisNeuron* cursor = config->neurons; cursor != NULL; cursor = cursor->next) {
		nodePairs[i] = cursor->id;
		nodePairs[i + 1] = cursor->ownerId;
		i += 2;
	}
	for (int j = 1; j < numberOfNodes; j++) {
		MPI_Send(nodePairs, config->neuronLength * 2, MPI_INT, j, METIS_CONFIG, MPI_COMM_WORLD);
	}

	int time = 0;
	int doneCount = 0;
	// Main event loop
	while (time < config->simulationLength) {
		MPI_Status status;
		int flag = 0;

		MPI_Iprobe(MPI_ANY_SOURCE, METIS_TASK_DONE, MPI_COMM_WORLD, &flag, &status);
		if (flag == 1) {
			int data[1];
			MPI_Recv(data, 1, MPI_INT, MPI_ANY_SOURCE, METIS_TASK_DONE, MPI_COMM_WORLD, &status);
			doneCount++;
		}

		if (doneCount == numberOfNodes - 1) {
			doneCount = 0;
			for (int i = 1; i < numberOfNodes; i++) {
				int data[1];
				MPI_Send(data, 1, MPI_INT, i, METIS_TIME_UPDATE, MPI_COMM_WORLD);
			}
			time++;
			if (DEBUG)
				printf("MASTER> Updating time %d\n", time);
		}
	}
	if (DEBUG)
		printf("MASTER> Waiting for all nodes to finish...\n");
	sleep(2);
}

void runWorkerNode(metisConfig* config, int id, int numberOfNodes) {
	// Initialize array to hold nodes I am responsible for
	int maxNumberOfNeuronsPerNode = config->neuronLength / (numberOfNodes - 1);
	if (config->neuronLength % (numberOfNodes - 1) != 0) {
		maxNumberOfNeuronsPerNode++;
	}

	int nodes[maxNumberOfNeuronsPerNode];
	// Set the starting nodes to -1 to indicate they are not assigned
	memset(nodes, -1, maxNumberOfNeuronsPerNode);


	MPI_Status status;
	int nodePairs[config->neuronLength * 2];
	MPI_Recv(nodes, maxNumberOfNeuronsPerNode, MPI_INT, MASTER, METIS_TASK, MPI_COMM_WORLD, &status);
	MPI_Recv(nodePairs, config->neuronLength * 2, MPI_INT, MASTER, METIS_CONFIG, MPI_COMM_WORLD, &status);
	int i = 0;
	while (nodes[i] != -1 && i < maxNumberOfNeuronsPerNode) {
		if (DEBUG)
			printf("WORKER %d> I am responsible for neuron %d\n", id, nodes[i]);
		i++;
	}

	for (i = 0; i < config->neuronLength * 2; i +=2) {
		for (metisNeuron* cursor = config->neurons; cursor != NULL; cursor = cursor->next) {
			if (nodePairs[i] == cursor->id) {
				cursor->ownerId = nodePairs[i + 1];
			}
		}
	}

	bool loadedAllData = false;
	bool needToSendDone = true;
	bool gettingData = false;
	bool needToHandleIO = true;
	int time = 0;

	int * buffer = malloc((sizeof(int) * (numberOfNodes - 1) * 2) + MPI_BSEND_OVERHEAD);
	MPI_Buffer_attach(buffer, (sizeof(int) * (numberOfNodes - 1) * 2) + MPI_BSEND_OVERHEAD);

	// Main event loop
	while (time < config->simulationLength) {
		int flag;
		MPI_Status status;

		if(DEBUG)
			printf("WORKER %d> On time unit %d\n", id, time);
		
		// Handle IO
		if (needToHandleIO)
		{
			metisIO* ioCursor = config->io;
			while (ioCursor != NULL) {
				if (ioCursor->type == 0) {
					metisIoConnection* ioConnCursor = ioCursor->connections;
					while (ioConnCursor != NULL) {
						if (arrayContains(nodes, maxNumberOfNeuronsPerNode, ioConnCursor->neuron->id)) {
							if (time >= ioCursor->offset && time < ioCursor->offset + ioCursor->duration) {
								if (DEBUG)
									printf("WORKER %d> Set neuron %s:%d to activity level 10\n", id, ioConnCursor->neuron->name, ioConnCursor->neuron->id);
								ioConnCursor->neuron->activityLevel = 10;
							}
						}
						ioConnCursor = ioConnCursor->next;
					}
				}
				ioCursor = ioCursor->next;
			}
			needToHandleIO = false;
		}

		// Check for data request
		MPI_Iprobe(MPI_ANY_SOURCE, METIS_DATA_REQUEST, MPI_COMM_WORLD, &flag, &status);
		if (flag == 1) {
			// Handle data request
			int data[2];
			
			MPI_Recv(data, 2, MPI_INT, MPI_ANY_SOURCE, METIS_DATA_REQUEST, MPI_COMM_WORLD, &status);

			if(DEBUG)
				printf("WORKER %d> Receiving data request from node %d\n", id, data[1]);

			// Locate the data in the config
			metisNeuron* cursor = config->neurons;
			while (cursor != NULL) {
				if (cursor->id == data[0]) {
					int response[3];
					response[0] = cursor->activityLevel;
					response[1] = id;
					response[2] = data[0];
					if (DEBUG)
						printf("WORKER %d> Send value %d to worker %d\n", id, response[0], data[1]);
					MPI_Bsend(response, 3, MPI_INT, data[1], METIS_DATA_RESPONSE, MPI_COMM_WORLD);
					break;
				}
				cursor = cursor->next;
			}
			if (cursor == NULL) {
				printf("WORKER %d> Failed to find node with id %d from worker %d\n", id, data[0], data[1]);
			}
		}

		MPI_Iprobe(MPI_ANY_SOURCE, METIS_DATA_RESPONSE, MPI_COMM_WORLD, &flag, &status);
		if (flag == 1) {
			int message[3];

			MPI_Recv(message, 3, MPI_INT, MPI_ANY_SOURCE, METIS_DATA_RESPONSE, MPI_COMM_WORLD, &status);

			if (DEBUG)
				printf("WORKER %d> Received data response from node %d\n", id, message[1]);
			metisNeuron* cursor = config->neurons;
			while (cursor != NULL) {
				if (cursor->id == message[2]) {
					if (DEBUG)
						printf("WORKER %d> Updated neuron %d with value %d from worker %d\n", id, message[2], message[0], message[1]);
					if (message[0] == -1) {
						cursor->activityLevel = 0;
					}
					else {
						cursor->activityLevel = message[0];
					}
					gettingData = false;
					break;
				}
				cursor = cursor->next;
			}
		}
		flag = 0;

		MPI_Iprobe(MASTER, METIS_TIME_UPDATE, MPI_COMM_WORLD, &flag, &status);
		if (flag == 1) {
			int data[1];

			MPI_Recv(data, 1, MPI_INT, MASTER, METIS_TIME_UPDATE, MPI_COMM_WORLD, &status);
			if (DEBUG)
				printf("WORKER %d> Received time update from master\n", id);

			if (id == 1 && OUTPUT_STATE) {
				metisNeuron* cursor = config->neurons;
				while (cursor != NULL) {
					printf("Time:%d\tNeuron:%d\tActivity Level:%d\n", time, cursor->id, cursor->activityLevel);
					cursor = cursor->next;
				}
			}

			metisNeuron* cursor = config->neurons;
			while (cursor != NULL) {
				if (arrayContains(nodes, maxNumberOfNeuronsPerNode, cursor->id)) {
					cursor->activityLevel = cursor->nextValue;
					cursor->nextValue = -1;
				}
				else {
					cursor->activityLevel = -1;
				}
				cursor = cursor->next;
			}
			needToSendDone = true;
			loadedAllData = false;
			gettingData = false;
			needToHandleIO = true;
			time++;
			if (DEBUG)
				printf("WORKER %d> Finished resetting after time step\n", id);
		}
		flag = 0;

		if (loadedAllData && needToSendDone) {
			// Send DONE and keep looping
			int done = 1;
			MPI_Bsend(&done, 1, MPI_INT, MASTER, METIS_TASK_DONE, MPI_COMM_WORLD);
			if (DEBUG)
				printf("WORKER %d> Sending DONE message\n", id);
			needToSendDone = false;
		}

		// Check if I have all of the data needed to calculate the next state of my neurons
		if (!loadedAllData) {
			//printf("WORKER %d> Has not received all data to calculate next state\n", id);
			metisNeuron* cursor = config->neurons;
			while (cursor != NULL) {
				if (arrayContains(nodes, maxNumberOfNeuronsPerNode, cursor->id)) {
					metisNeuronConnection* connCursor = cursor->connections;
					int i = 0;
					while (connCursor != NULL) {
						if (connCursor->neuron->activityLevel == -1) {
							if (!arrayContains(nodes, maxNumberOfNeuronsPerNode, connCursor->neuron->id)) {
								if (!gettingData) {
									// Get the value from the responsible node
									int data[2];
									data[0] = connCursor->neuron->id;
									data[1] = id;
									if (DEBUG)
										printf("WORKER %d> Requesting info about neuron %d from node %d\n", id, data[0], connCursor->neuron->ownerId);

									MPI_Bsend(data, 2, MPI_INT, connCursor->neuron->ownerId, METIS_DATA_REQUEST, MPI_COMM_WORLD);
									gettingData = true;
								}
							}
							else {
								connCursor->neuron->activityLevel = 0;
							}
						}
						else {
							i++;
							if (DEBUG)
								printf("WORKER %d> Data found... %d out of %d\n", id, i, cursor->connectionsLength);
						}
						connCursor = connCursor->next;
					}
					if (i == cursor->connectionsLength) {
						// Calculate next value
						connCursor = cursor->connections;
						double total = 0;
						while (connCursor != NULL) {
							total += connCursor->sensitivity * connCursor->neuron->activityLevel;
							connCursor = connCursor->next;
						}

						if (total <= 10)
							cursor->nextValue = total;
						else
							cursor->nextValue = 10;
					}
				}
				cursor = cursor->next;
			}
		}

		// Check if all data is loaded
		if (!loadedAllData) {
			loadedAllData = true;
			metisNeuron* cursor = config->neurons;
			while (cursor != NULL) {
				if (arrayContains(nodes, maxNumberOfNeuronsPerNode, cursor->id) && cursor->nextValue == -1) {
					loadedAllData = false;
				}
				cursor = cursor->next;
			}
		}
	}
	free(buffer);
}

bool arrayContains(int arr[], int arrayLength, int val) {
	for (int i = 0; i < arrayLength; i++) {
		if (arr[i] == val) {
			return true;
		}
	}
	return false;
}

cJSON* parseFile(char* filename) {
	char* buffer = 0;
	int length;
	cJSON* output = 0;
	FILE* f = fopen(filename, "rb");

	if (!f) {
		fprintf(stderr, "Failed to open file '%s'\n", filename);
		return NULL;
	} 
	
	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);
	buffer = malloc(length);
	if (buffer)
	{
		fread(buffer, 1, length, f);
	}
	fclose(f);

	// Convert string into cJSON struct
	output = cJSON_Parse(buffer);

	if (output == NULL) {
		const char* error = cJSON_GetErrorPtr();
		if (error != NULL) {
			fprintf(stderr, "Error before: %s\n", error);
			return NULL;
		}
	}

	return output;
}

metisConfig* parseConfig(cJSON* config) {
	// Convert cJSON structs to metisConfig and free cJSON
	const cJSON* neurons = NULL;
	const cJSON* simLength = NULL;
	const cJSON* io = NULL;
	const cJSON* neuron = NULL;
	const cJSON* name = NULL;
	const cJSON* connections = NULL;
	const cJSON* connection = NULL;
	const cJSON* sensitivity = NULL;
	const cJSON* connectionNeuronName = NULL;
	const cJSON* ioElement = NULL;
	const cJSON* type = NULL;
	const cJSON* element = NULL;
	metisConfig* mConfig = NULL;
	metisNeuron* mNeuron = NULL;
	metisNeuronConnection* mConnection = NULL;
	metisIO* mIO = NULL;
	metisIoConnection* ioConnection = NULL;

	mConfig = metisNewConfig();

	simLength = cJSON_GetObjectItemCaseSensitive(config, "simulationLength");
	if (!cJSON_IsNumber(simLength)) {
		fprintf(stderr, "Failed to read simulationLength! Is the field 'simulationLength' an integer with more with a value greater than 0?\n");
		return NULL;
	}
	mConfig->simulationLength = simLength->valueint;

	// read neuron list
	neurons = cJSON_GetObjectItemCaseSensitive(config, "neurons");
	if (!cJSON_IsArray(neurons) || cJSON_GetArraySize(neurons) == 0) {
		fprintf(stderr, "Failed to read neuron list! Is the field 'neurons' an array with more than 0 elements?\n");
		return NULL;
	}

	// First add all neurons
	cJSON_ArrayForEach(neuron, neurons) {
		mNeuron = metisNewNeuron();

		// Get name value from neuron json
		name = cJSON_GetObjectItemCaseSensitive(neuron, "name");
		if (!cJSON_IsString(name) || (name->valuestring == NULL)) {
			fprintf(stderr, "Failed to get 'name' field from neuron! Make sure your json is properly validated\n");
			return NULL;
		}

		// Safely copy the neuron name into the struct
		strncpy(mNeuron->name, name->valuestring, METIS_MAX_NUERON_NAME);

		// Add the neuron to the config
		metisConfigAddNeuron(mConfig, mNeuron);
	}

	neuron = NULL;

	// Then add all connections between neurons
	cJSON_ArrayForEach(neuron, neurons) {
		// Get connection array from neuron
		connections = cJSON_GetObjectItemCaseSensitive(neuron, "connections");
		name = cJSON_GetObjectItemCaseSensitive(neuron, "name");

		if (cJSON_GetArraySize(connections) > 0) {
			// Iterate through the connections
			cJSON_ArrayForEach(connection, connections) {
				// Get connection sensitivity
				sensitivity = cJSON_GetObjectItemCaseSensitive(connection, "sensitivity");
				if (!cJSON_IsNumber(sensitivity)) {
					fprintf(stderr, "Sensitivity value of connection is not a number!\n");
					return NULL;
				}

				// Get connection neuron name
				connectionNeuronName = cJSON_GetObjectItemCaseSensitive(connection, "neuron");
				if (!cJSON_IsString(connectionNeuronName) || (connectionNeuronName->valuestring == NULL)) {
					fprintf(stderr, "Failed to get 'name' field from connection! Make sure your json is properly validated\n");
					return NULL;
				}

				mNeuron = metisGetNeuronByName(mConfig, connectionNeuronName->valuestring);
				if (mNeuron == NULL) {
					// Failed to find neuron
					fprintf(stderr, "Failed to find neuron referenced in connection!\n");
					return NULL;
				}

				mConnection = metisNewNeuronConnection(mNeuron, sensitivity->valuedouble);

				metisAddNeuronConnection(metisGetNeuronByName(mConfig, name->valuestring), mConnection);
			}
		}

	}

	// Add IO connections
	io = cJSON_GetObjectItemCaseSensitive(config, "io");
	if (!cJSON_IsArray(io) || cJSON_GetArraySize(io) == 0) {
		fprintf(stderr, "Failed to read io list! Is the field 'io' an array with more than 0 elements?\n");
		return NULL;
	}

	cJSON_ArrayForEach(ioElement, io) {
		mIO = metisNewIO();

		// Get IO name
		name = cJSON_GetObjectItemCaseSensitive(ioElement, "name");
		if (!cJSON_IsString(name) || (name->valuestring == NULL)) {
			fprintf(stderr, "Failed to get 'name' field from io element! Make sure your json is properly validated\n");
			return NULL;
		}

		strncpy(mIO->name, name->valuestring, METIS_MAX_IO_NAME);

		// Get IO type
		type = cJSON_GetObjectItemCaseSensitive(ioElement, "type");
		if (!cJSON_IsNumber(type) || type->valueint < 0 || type->valueint > 1) {
			fprintf(stderr, "Invalid 'type' field from io element '%s'! Make sure your json is properly validated\n", mIO->name);
			return NULL;
		}

		mIO->type = type->valueint;

		switch (mIO->type) {
		case 0:
			// Stimulus
			// Get duration
			element = cJSON_GetObjectItemCaseSensitive(ioElement, "duration");
			if (!cJSON_IsNumber(element)) {
				fprintf(stderr, "Invalid 'duration' field from io element '%s'! Make sure your json is properly validated\n", mIO->name);
				return NULL;
			}
			mIO->duration = element->valueint;

			// Get offset
			element = cJSON_GetObjectItemCaseSensitive(ioElement, "offset");
			if (!cJSON_IsNumber(element)) {
				fprintf(stderr, "Invalid 'offset' field from io element '%s'! Make sure your json is properly validated\n", mIO->name);
				return NULL;
			}
			mIO->offset = element->valueint;

			// Get amplitude
			element = cJSON_GetObjectItemCaseSensitive(ioElement, "amplitude");
			if (!cJSON_IsNumber(element)) {
				fprintf(stderr, "Invalid 'amplitude' field from io element '%s'! Make sure your json is properly validated\n", mIO->name);
				return NULL;
			}
			mIO->amplitude = element->valueint;
			break;
		case 1:
			// Reader
			// Get outputPrefix
			element = cJSON_GetObjectItemCaseSensitive(ioElement, "outputPrefix");
			if (!cJSON_IsString(element)) {
				fprintf(stderr, "Invalid 'outputPrefix' field from io element '%s'! Make sure your json is properly validated\n", mIO->name);
				return NULL;
			}
			strncpy(mIO->outputPrefix, element->valuestring, METIX_MAX_IO_OUTPUT_PREFIX);
			break;
		default:
			fprintf(stderr, "Invalid type found! This shouldn't be possible. Please check the code value constraints\n");
			return NULL;
		}

		// Create connections for each io element
		connections = cJSON_GetObjectItemCaseSensitive(ioElement, "connections");
		if (!cJSON_IsArray(connections) || cJSON_GetArraySize(connections) == 0) {
			fprintf(stderr, "Failed to read connections list! Is the field 'connections' an array with more than 0 elements?\n");
			return NULL;
		}

		cJSON_ArrayForEach(connection, connections) {
			ioConnection = metisNewIoConnection();

			name = cJSON_GetObjectItemCaseSensitive(connection, "neuron");
			if (!cJSON_IsString(name) || (name->valuestring == NULL)) {
				fprintf(stderr, "Failed to get 'name' field from io connection element! Make sure your json is properly validated\n");
				return NULL;
			}

			// Find neuron
			mNeuron = metisGetNeuronByName(mConfig, name->valuestring);
			if (mNeuron == NULL) {
				fprintf(stderr, "Failed to find neuron referenced by io element! Neuron name: %s\n", name->valuestring);
				return NULL;
			}

			ioConnection->neuron = mNeuron;

			metisAddIOConnection(mIO, ioConnection);
		}

		metisConfigAddIO(mConfig, mIO);
	}

	cJSON_Delete(config);

	return mConfig;
}

metisNeuron* metisGetNeuronByName(metisConfig* config, char* name) {
	metisNeuron* cursor = NULL;

	if (config->neuronLength == 0) {
		return NULL;
	}

	cursor = config->neurons;
	while (strncmp(cursor->name, name, METIS_MAX_NUERON_NAME) != 0) {
		if (cursor->next == NULL) {
			return NULL;
		}
		cursor = cursor->next;
	}

	return cursor;
}

metisNeuronConnection* metisNewNeuronConnection(metisNeuron* neuron, double sensitivity) {
	metisNeuronConnection* newConnection = NULL;

	newConnection = malloc(sizeof(metisNeuronConnection));

	newConnection->neuron = neuron;
	newConnection->sensitivity = sensitivity;
	newConnection->next = NULL;

	return newConnection;
}

void metisAddConnection(metisNeuronConnection* base, metisNeuronConnection* connection) {
	metisNeuronConnection* cursor = NULL;

	// Traverse linked list to find the end
	cursor = base;
	while (cursor->next != NULL) {
		cursor = cursor->next;
	}

	// Set the reference to the new connection
	cursor->next = connection;
}

void metisAddIoConnection(metisIoConnection* base, metisIoConnection* connection) {
	metisIoConnection* cursor = NULL;

	// Traverse linked list to find the end
	cursor = base;
	while (cursor->next != NULL) {
		cursor = cursor->next;
	}

	// Set the reference to the new connection
	cursor->next = connection;
}

metisConfig* metisNewConfig() {
	metisConfig* newConfig;

	newConfig = malloc(sizeof(metisConfig));
	
	// Guarentee all fields are properly cleared
	newConfig->neurons = NULL;
	newConfig->neuronLength = 0;
	newConfig->io = NULL;
	newConfig->ioLength = 0;

	return newConfig;
}

metisIO* metisNewIO() {
	metisIO* newIO;

	newIO = malloc(sizeof(metisIO));

	// Guarentee all fields are properly cleared
	newIO->amplitude = 0;
	newIO->connections = NULL;
	newIO->connectionsLength = 0;
	newIO->duration = 0;
	newIO->offset = 0;
	newIO->next = NULL;
	newIO->type = -1;

	return newIO;
}

metisNeuron* metisNewNeuron() {
	metisNeuron* newNeuron;

	newNeuron = malloc(sizeof(metisNeuron));

	// Guarentee all fields are properly cleared
	newNeuron->connections = NULL;
	newNeuron->connectionsLength = 0;
	newNeuron->next = NULL;
	newNeuron->ownerId = -1;
	newNeuron->nextValue = -1;
	newNeuron->activityLevel = -1;

	return newNeuron;
}

metisIoConnection* metisNewIoConnection() {
	metisIoConnection* newConnection;

	newConnection = malloc(sizeof(metisIoConnection));
	newConnection->neuron = NULL;
	newConnection->next = NULL;

	return newConnection;
}

void metisAddNeuronConnection(metisNeuron* neuron, metisNeuronConnection* connection) {
	if (neuron->connectionsLength == 0) {
		neuron->connections = connection;
		neuron->connectionsLength = 1;
		return;
	}

	metisAddConnection(neuron->connections, connection);
	neuron->connectionsLength++;
}

void metisAddIOConnection(metisIO* io, metisIoConnection* connection) {
	if (io->connectionsLength == 0) {
		io->connections = connection;
		io->connectionsLength = 1;
		return;
	}

	metisAddIoConnection(io->connections, connection);
	io->connectionsLength++;
}

void metisConfigAddNeuron(metisConfig* config, metisNeuron* neuron) {
	metisNeuron* cursor = NULL;
	
	if (config->neuronLength == 0) {
		config->neurons = neuron;
		config->neuronLength = 1;
		neuron->id = 0;
		return;
	}

	// Traverse neruon list
	cursor = config->neurons;
	while (cursor->next != NULL) {
		cursor = cursor->next;
	}

	neuron->id = config->neuronLength;
	cursor->next = neuron;
	config->neuronLength++;
}

void metisConfigAddIO(metisConfig* config, metisIO* io) {
	metisIO* cursor = NULL;

	if (config->ioLength == 0) {
		config->io = io;
		config->ioLength = 1;
		return;
	}

	// Traverse io list
	cursor = config->io;
	while (cursor->next != NULL) {
		cursor = cursor->next;
	}

	cursor->next = io;
	config->ioLength++;
}

void metisFreeConfig(metisConfig* config) {
	metisFreeIO(config->io);
	metisFreeNeuron(config->neurons);
	config->io = NULL;
	config->neurons = NULL;
	config->ioLength = 0;
	config->neuronLength = 0;
}

void metisFreeIO(metisIO* io) {
	metisIO* last = io;
	metisIO* next = NULL;

	while (last != NULL) {
		next = last->next;
		// Free all metisIoConnections
		metisFreeIoConnections(last->connections);
		free(last);
		last = next;
	}
}

void metisFreeNeuron(metisNeuron* neuron) {
	metisNeuron* last = neuron;
	metisNeuron* next = NULL;

	while (last != NULL) {
		next = last->next;
		// Free all metisIoConnections
		metisFreeNeuronConnections(last->connections);
		free(last);
		last = next;
	}
}

void metisFreeIoConnections(metisIoConnection* connection) {
	metisIoConnection* last = connection;
	metisIoConnection* next = NULL;
	
	while (last != NULL) {
		next = last->next;
		free(last);
		last = next;
	}
}

void metisFreeNeuronConnections(metisNeuronConnection* connection) {
	metisNeuronConnection* last = connection;
	metisNeuronConnection* next = NULL;

	while (last != NULL) {
		next = last->next;
		free(last);
		last = next;
	}
}