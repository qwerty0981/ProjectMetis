#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

#define METIS_MAX_NUERON_NAME 20
#define METIS_MAX_IO_NAME 20
#define METIX_MAX_IO_OUTPUT_PREFIX 20

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
	struct metisNeuron* next;
} metisNeuron;

typedef struct metisIO {
	char name[METIS_MAX_IO_NAME];
	int type;								// 0 = stimulus, 1 = reader
	metisIoConnection* connections;
	struct metisIO* next;
	int connectionsLength;
	int duration;
	int amplitude;
	char outputPrefix[METIX_MAX_IO_OUTPUT_PREFIX];
} metisIO;

typedef struct metisConfig {
	metisNeuron* neurons;
	int neuronLength;
	metisIO* io;
	int ioLength;
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

	// Print off a hello world message
	printf("Hello world from processor %s, rank %d out of %d processors\n",
			processor_name, world_rank, world_size);

	//const char filename[] = "/home/clong4947/projects/metis/test.json";
	const char filename[] = "model.json";

	cJSON* file = parseFile(filename);
	if (file == NULL) {
		fprintf(stderr, "Failed to parse file '%s'\n", filename);
		return 1;
	}
	metisConfig* config = parseConfig(file);
	if (config == NULL) {
		return 1;
	}

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

	// Test printing from different nodes
	if (world_rank == 0) {
		// I am master
		printf("I am the master node and I am responsible for distributing the neurons out to each working node\n");
	}
	else {
		// I am a worker node
		printf("I am worker node: %d and I am responsible for handling the computations related to different neurons\n", world_rank);
	}

	// Clean Up the memory used by our config object
	metisFreeConfig(config);

	printf("Successfully freed all memory used by metis config object\n");

	// Finalize the MPI environment.
	MPI_Finalize();

	return 0;
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

	printf("Loaded file with value: %s\n", buffer);

	// Convert string into cJSON struct
	output = cJSON_Parse(buffer);

	if (output == NULL) {
		const char* error = cJSON_GetErrorPtr();
		if (error != NULL) {
			fprintf(stderr, "Error before: %s\n", error);
			return NULL;
		}
	}

	printf("Finished parsing\n");

	return output;
}

metisConfig* parseConfig(cJSON* config) {
	// Convert cJSON structs to metisConfig and free cJSON
	const cJSON* neurons = NULL;
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

	// First, read neuron list
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

	printf("Added all neurons\n");
	neuron = NULL;

	// Then add all connections between neurons
	cJSON_ArrayForEach(neuron, neurons) {
		printf("Starting neuron pass\n");
		// Get connection array from neuron
		connections = cJSON_GetObjectItemCaseSensitive(neuron, "connections");
		name = cJSON_GetObjectItemCaseSensitive(neuron, "name");

		if (cJSON_GetArraySize(connections) > 0) {
			// Iterate through the connections
			cJSON_ArrayForEach(connection, connections) {
				// Get connection sensitivity
				printf("Iterating through connections\n");
				sensitivity = cJSON_GetObjectItemCaseSensitive(connection, "sensitivity");
				if (!cJSON_IsNumber(sensitivity)) {
					fprintf(stderr, "Sensitivity value of connection is not a number!\n");
					return NULL;
				}

				printf("Got sensitivity\n");

				// Get connection neuron name
				connectionNeuronName = cJSON_GetObjectItemCaseSensitive(connection, "neuron");
				if (!cJSON_IsString(connectionNeuronName) || (connectionNeuronName->valuestring == NULL)) {
					fprintf(stderr, "Failed to get 'name' field from connection! Make sure your json is properly validated\n");
					return NULL;
				}

				printf("Got neuron name\n");

				mNeuron = metisGetNeuronByName(mConfig, connectionNeuronName->valuestring);
				printf("Got neuron by name\n");
				if (mNeuron == NULL) {
					// Failed to find neuron
					fprintf(stderr, "Failed to find neuron referenced in connection!\n");
					return NULL;
				}

				mConnection = metisNewNeuronConnection(mNeuron, sensitivity->valuedouble);
				printf("Created new connection struct\n");

				metisAddNeuronConnection(metisGetNeuronByName(mConfig, name->valuestring), mConnection);
			}
		}

	}

	printf("Mapped all connections\n");

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
		return;
	}

	// Traverse neruon list
	cursor = config->neurons;
	while (cursor->next != NULL) {
		cursor = cursor->next;
	}

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