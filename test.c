// Cameron O'Neill (23340022)
// Akhil Gorasia (23424609)
// Martin Mitanoski (23385544)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h> 

#define VERBOSE true            // Verbose printing
#define DEBUG true              // Debug Printing 
#define MAX_ENTRIES 100         // Arbitrary limit
#define TCP_BACKLOG_LENGTH 10   // Number of TCP connections the TCP socket will hold up to
#define MAX_NEIGHBORS 20        // Number of possible neighbors a station can accomadate 
#define MAX_STATION_NAME 32     // Maximum station name length 
#define MAX_ID_LENGTH 48        // Maximum ID length (packetID/routeID) 
#define MAX_IP_LEGNTH 24        // Maximum length of an IP address
#define MAX_PACKET_TYPE_LENGTH 16  // Maximum length of packet type
#define MAX_JSON_BUFFER 2048
#define MAX_NAME_LENGTH 32

#define PING "PING"
#define ACK "ACK"
#define ROUTE_QUERY "ROUTE_QUERY"
#define ROUTE_SUCCESS "ROUTE_SUCCESS"
#define ROUTE_FAIL "ROUTE_FAIL"

// Printing messages
#define DEBUG_PRINT(msg) if (DEBUG) { printf("[DEBUG %d] %s\n", STATION_IDENTIFIER, msg); }
#define VERBOSE_PRINT(msg) if (VERBOSE) { printf("[INFO %d] %s\n", STATION_IDENTIFIER, msg); }
#define ERROR_PRINT(msg) printf("[ERROR %d] %s\n", STATION_IDENTIFIER, msg)
#define VERBOSE_PRINT_EMPTY() if (VERBOSE) { printf("\n"); }

// Structures
extern struct sent_message {
    char packetID[MAX_ID_LENGTH];  // UUID length
    char packetType[MAX_PACKET_TYPE_LENGTH];
    time_t time_latestSent;
    int attempts;
    struct sockaddr_in targetAddress;
    char packet[MAX_JSON_BUFFER];
} sent_messages[100];  

extern struct active_routingRequest {
    char destination[MAX_STATION_NAME];
    char responses[MAX_JSON_BUFFER];  
    time_t time_sent;
    int socket;
    char request_id[MAX_ID_LENGTH];  // Adding request_id here for tracking
} active_routingRequests[100];

extern int active_routingRequests_count; 

typedef struct NeighborInfo {
    char stationName[MAX_STATION_NAME];
    char ip[256];
    int port;
} NeighborInfo;

extern NeighborInfo active_neighbors[MAX_NEIGHBORS]; 

typedef struct {
    char departure_time[16];
    char service_name[MAX_STATION_NAME];
    char departure_station[MAX_STATION_NAME];
    char arrival_time[16];
    char arrival_station[MAX_STATION_NAME];
} TimetableEntry;

typedef struct {
    char station_name[MAX_STATION_NAME];
    TimetableEntry entries[MAX_ENTRIES];
    int num_entries;
    char timetable_file[64];
    float location[2];
    time_t mod_time;
} StationTimetable;

// Packet structure
typedef struct {
    char type[16];
    char originStationName[MAX_STATION_NAME];
    char packetID[MAX_ID_LENGTH];
    char msg[MAX_JSON_BUFFER];
} UDP_Packet;

typedef struct {
    char station_name[MAX_STATION_NAME];
    char arrival_time[6];
    char departure_time[6];
    char service_name[MAX_STATION_NAME];
} VisitedStation;

typedef struct {
    char destination[MAX_STATION_NAME];
    char time[6];  // Changed from time_t to char[6]
    VisitedStation visited[MAX_ENTRIES];
    char original_departure_time[6];
    char original_service[MAX_STATION_NAME];
    char request_id[MAX_ID_LENGTH];
    int visited_count;
} RoutingRequest;

typedef struct {
    char depature_time[6];
    char type[16];
    char arrival_time[6];
    char destination[MAX_STATION_NAME];
    char original_time[6];
    char leaving_service[MAX_STATION_NAME];
    char route[MAX_JSON_BUFFER];
    char request_id[MAX_ID_LENGTH];
    int visited_index;
    VisitedStation visited[MAX_ENTRIES];
    int visited_count; 
} RoutingResponse;

// Global variables
struct sent_message sent_messages[100];             // Sent packets that need to be ACKnowledged
int sent_messages_count = 0;                        // Number of active send messages (havent been ACK'd)
StationTimetable station;                           // Station timetable for the station the server is acting on
int TCP_SOCKET, UDP_SOCKET;                         //
int TCP_PORT, UDP_PORT;                             // Port number for TCP and UDP socket
struct sockaddr_in client_address;                  // 
int TCP_CONNECTION;                                 // The socket descriptor for the TCP connection
char *STATION_NAME;                                 // Station name the server is acting on
char STATION_IDENTIFIER;                            // Single character to identify station in console print messages
char **NEIGHBORS;                                   // Station's possible neighbors (inactive/active)
int NEIGHBOR_COUNT;                                 // Number of possible neighbors to station
struct NeighborInfo active_neighbors[MAX_NEIGHBORS]; // List of active neighbors
int active_neighbor_count = 0;                          // Number of active neighbors
struct active_routingRequest active_routingRequests[100];
int active_routingRequests_count = 0;


// Function prototypes
void parse_arguments(int argc, char *argv[]);
void init_station_timetable(StationTimetable *timetable, const char *station_name);
void setup_sockets(void);
void load_timetable(StationTimetable *timetable);
void handle_client_connection(int tcp_socket);
void TCP_sendMessage(int sock, const char *msg);
TimetableEntry *find_direct_or_query_indirect(const char *selected_time, const char *selected_destination, int tcp_socket);
int time_to_minutes(const char *time_str);
void error_print(const char *message);
void UDP_sendMessage(const char *ip, int port, const char *packetType, const char *msg, const char *ACK_responseID, int resendIndex);
void pingNeighbors(bool all, bool inactive);
void deserialize_udp_packet(char *data, UDP_Packet *packet);
void handle_udp_message(char *data, struct sockaddr_in addr);
void extract_json_value(char *result, size_t max_len, const char *data, const char *key);
void query_neighbors(RoutingRequest *message);
void serialize_routing_request(const RoutingRequest *request, char *buffer, size_t buf_size);
void deserialize_routing_request(const char *data, RoutingRequest *request);
TimetableEntry *find_next_departure(StationTimetable *timetable, const char *target_time_str, const char *destination);
void generate_unique_id(char *buffer, size_t size);
void print_active_neighbors(void);
time_t get_mod_time(const char *filename);
void update_timetable(StationTimetable *timetable);
bool is_time_after_last_departure(const char *selected_time);
void serialize_routing_response(const RoutingResponse *response, char *buffer, size_t buf_size);
void deserialize_routing_response(const char *data, RoutingResponse *response);
void decode_url(char *dst, const char *src);
void construct_route_message(RoutingResponse *response, char *route_message, size_t size);
void deserialize_visited_station(const char *data, VisitedStation *vstation);
void append_to_visited(RoutingRequest *request, const char *station_name, const char *arrival_time, const char *departure_time, const char *service_name);
void append_to_visitedR(RoutingResponse *request, const char *station_name, const char *arrival_time, const char *departure_time, const char *service_name);

int main(int argc, char *argv[]) {
    struct timeval timeout;
    fd_set readfds, exceptfds;
    int activity, pingCounter = 0;

    parse_arguments(argc, argv);
    init_station_timetable(&station, argv[1]); // Station is a global variable storing the information for this station 

    setup_sockets();

    int max_sd = TCP_SOCKET > UDP_SOCKET ? TCP_SOCKET : UDP_SOCKET;

    sleep(3); 
    pingNeighbors(true, false); // Initial ping to ALL neighbors
    while (1) {
        sleep(2);
        pingCounter++;

        // Check resend packets
        // func here  -- INCOMPLETE

        // Check active routing requests
        // func here  -- INCOMPLETE

        // Ping inactive neighbours
        if (pingCounter >= 5) {
          update_timetable(&station);
          //pingNeighbors(false, true); // Ping inactive neighbors -- INCOMPLETE
          pingCounter = 0;
        }

        // Reset file descriptor sets and timeout on each iteration
        FD_ZERO(&readfds);
        FD_ZERO(&exceptfds);

        FD_SET(TCP_SOCKET, &readfds);
        FD_SET(UDP_SOCKET, &readfds);
        FD_SET(TCP_SOCKET, &exceptfds);
        FD_SET(UDP_SOCKET, &exceptfds);

        // Hold select for at most 0.1 seconds
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 0.1 seconds
        activity = select(max_sd + 1, &readfds, NULL, &exceptfds, &timeout);

        if (activity < 0 && errno != EINTR) {
            perror("Select error");
            continue;
        }

        

        if (FD_ISSET(TCP_SOCKET, &readfds)) {
            printf("TCP MESSAGE ACCEPTED\n");
            socklen_t addrlen = sizeof(client_address);
            TCP_CONNECTION = accept(TCP_SOCKET, (struct sockaddr *)&client_address, &addrlen);
            if (TCP_CONNECTION < 0) {
                perror("Accept failed");
            } else {
                fcntl(TCP_CONNECTION, F_SETFL, O_NONBLOCK); // Set non-blocking mode
                handle_client_connection(TCP_CONNECTION);
            }
        }

        if (FD_ISSET(UDP_SOCKET, &readfds)) {
            char buffer[4096];
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            ssize_t bytes_received = recvfrom(UDP_SOCKET, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &len);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0'; 
                handle_udp_message(buffer, addr);
            } else if (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Failed to receive UDP packet");
            }
        }

        if (FD_ISSET(TCP_SOCKET, &exceptfds) || FD_ISSET(UDP_SOCKET, &exceptfds)) {
            perror("Socket exception");
            // Handle socket exceptions or errors here
        }

    }

    return 0;
}


void error_print(const char *message) {
    fprintf(stderr, "%s\n", message);
}


int time_to_minutes(const char *time_str) {
    int hours, minutes;
    sscanf(time_str, "%d:%d", &hours, &minutes);
    return hours * 60 + minutes;
}


void parse_arguments(int argc, char *argv[]) {
    if (argc < 5) {
        error_print("Usage: ./station-server <Station-Name> <TCP-Port> <UDP-Port> <Neighbor1> ...");
        exit(EXIT_FAILURE);
    }

    STATION_NAME = argv[1];
    TCP_PORT = atoi(argv[2]);
    UDP_PORT = atoi(argv[3]);

    NEIGHBORS = &argv[4];
    NEIGHBOR_COUNT = argc - 4;

    STATION_IDENTIFIER = STATION_NAME[strlen(STATION_NAME) - 1];  // Last character of the station name
}

int count_stations(const char *route) {
    int count = 0;
    const char *ptr = route;

    while ((ptr = strchr(ptr, ';')) != NULL) {
        count++;
        ptr++; 
    }
    if (count > 0) {
        count++;
    }

    return count;
}


// --------------------------------------------------------- HANDLE UDP MESSAGE ---------------------------------------------------------

void handle_udp_message(char *data, struct sockaddr_in addr) {
    UDP_Packet packet;
    deserialize_udp_packet(data, &packet);
    
    printf("%s --> Full UDP Packet Received: Type: %s, Origin: %s, ID: %s\n", STATION_NAME, packet.type, packet.originStationName, packet.packetID);
    printf("%s --> Packet: %s \n", STATION_NAME, packet.msg);
    printf("%s --> Packet TYPE: %s \n", STATION_NAME, packet.type);
    // Get sender's information (IP/PORT) AND send ACK
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
    int port = ntohs(addr.sin_port);

    if (strcmp(packet.type, ROUTE_QUERY) == 0) {
        printf("Received ROUTE_QUERY from %s:%d\n", ip, port);
        UDP_sendMessage(ip, port, "ACK", NULL, packet.packetID, -1);

        RoutingRequest message;
        deserialize_routing_request(packet.msg, &message);

        // Check if the current station has already been visited
        bool already_visited = false;
        for (int i = 0; i < message.visited_count; i++) {
            if (strcmp(message.visited[i].station_name, STATION_NAME) == 0) {
                already_visited = true;
                break;
            }
        }

        if (already_visited) {
            printf("Query already processed here, dropping to prevent loop.\n");
            already_visited = false;
            return;
        }

        printf("Processing query for destination: %s\n", message.destination);

        TimetableEntry *departure = find_next_departure(&station, message.time, message.destination);
        if (departure) {
            printf("Route found. Sending back success.\n");

            RoutingResponse response;

            

            strcpy(response.depature_time, departure->departure_time);
            strcpy(response.type, "success");
            strcpy(response.arrival_time, departure->arrival_time);
            strcpy(response.destination, departure->arrival_station);
            strcpy(response.original_time, message.original_departure_time);
            strcpy(response.leaving_service, departure->service_name);
            strcpy(response.request_id, message.request_id);
            response.visited_count = message.visited_count;
            for (int i = 0; i < message.visited_count; i++) {
                response.visited[i] = message.visited[i];
            }
            append_to_visitedR(&response, STATION_NAME, departure->departure_time, departure->arrival_time, departure->service_name);
            response.visited_index = message.visited_count - 2;
            
            char response_data[MAX_JSON_BUFFER];
            serialize_routing_response(&response, response_data, sizeof(response_data));
            UDP_sendMessage(ip, port, "ROUTE_SUCCESS", response_data, NULL, -1);
        } else {
            printf("No departure available. Querying further neighbors.\n");
            query_neighbors(&message);
        }
    } else if (strcmp(packet.type, PING) == 0) {
        printf("Received PING from %s:%d\n", ip, port);
        UDP_sendMessage(ip, port, ACK, NULL, packet.packetID, -1);
    } else if (strcmp(packet.type, ACK) == 0) {
        printf("ACK received for Packet ID: %s\n", packet.packetID);
        for (int i = 0; i < sent_messages_count; i++) {
            if (strcmp(sent_messages[i].packetID, packet.packetID) == 0) {
                if (strcmp(sent_messages[i].packetType, PING) == 0) {
                    printf("UDP Neighbor Discovered: %s (%s:%d)\n", packet.originStationName, ip, port);
                    struct NeighborInfo *neighbor = &active_neighbors[active_neighbor_count];
                    strcpy(neighbor->stationName, packet.originStationName);
                    strcpy(neighbor->ip, ip);
                    neighbor->port = port;
                    active_neighbor_count++;
                }
                for (int j = i; j < sent_messages_count - 1; j++) {
                    sent_messages[j] = sent_messages[j + 1];
                }
                sent_messages_count--;
                printf("Deleted packet ID: %s from sent messages\n", packet.packetID);
                break;
            }
        }
    } else if (strcmp(packet.type, ROUTE_SUCCESS) == 0) {
        printf("%s --> ROUTE_SUCCESS received from %s:%d\n", STATION_NAME, ip, port);
        RoutingResponse response;
        deserialize_routing_response(packet.msg, &response);
        printf("%s --> Visited Index: %i\n", STATION_NAME, response.visited_index);
        printf("%s --> Request_id: %s\n", STATION_NAME, response.request_id);
        printf("%s --> Active Request count: %i\n", STATION_NAME, active_routingRequests_count);
            if (response.visited_index <= -1) {
                // Route response has returned to the original station
                for (int i = 0; i < active_routingRequests_count; i++) {
                    if (strcmp(active_routingRequests[i].request_id, response.request_id) == 0) {
                        printf("%s --> SENIND SUCCESSFUL MESSAGE TO TCP\n", STATION_NAME);
                        char route_message[MAX_JSON_BUFFER];
                        construct_route_message(&response, route_message, sizeof(route_message));
                        TCP_sendMessage(active_routingRequests[i].socket, route_message);
                        
                        // Optional (we dont have to remove active routing requests)
                        // Removing the active routing request from the array
                        for (int j = i; j < active_routingRequests_count - 1; j++) {
                            active_routingRequests[j] = active_routingRequests[j + 1];
                        }
                        active_routingRequests_count--;
                        break;
                    }
                }
            } else {
                printf("%s --> NOT ORIGIN STATION, SENDING TO NEIGHBOUR\n", STATION_NAME);

                // Route response needs to be sent to the next neighbor
                int index = response.visited_index;
                printf("INDEX: %d\n", index);
                printf("VISITED[0].name: %s\n", response.visited[0].station_name);
                // Check if the index is valid
                if (index >= 0 && index < response.visited_count) {
                    // Get the next neighbor station name
                    const char *next_neighbor = response.visited[index].station_name;

                    printf("%s --> NEXT NEIGHBOUR TO SEND TO: %s\n", STATION_NAME, next_neighbor);

                    // Update the response
                    response.visited_index = response.visited_index - 1;
                    strcpy(response.type, "success");

                    // Serialize the updated RoutingResponse object
                    char response_data[MAX_JSON_BUFFER];
                    serialize_routing_response(&response, response_data, sizeof(response_data));

                    // Find the neighbor's IP and port
                    for (int i = 0; i < active_neighbor_count; i++) {
                        if (strcmp(active_neighbors[i].stationName, next_neighbor) == 0) {
                            UDP_sendMessage(active_neighbors[i].ip, active_neighbors[i].port, "ROUTE_SUCCESS", response_data, NULL, -1);
                            break;
                        }
                    }
                }
            }
    } else {
        printf("Unknown packet type: %s\n", packet.type);
    }
}

// --------------------------------------------------------- CONSTRUCT RESPONSE MESSAGE ---------------------------------------------------------

void construct_route_message(RoutingResponse *response, char *route_message, size_t size) {
    snprintf(route_message, size, "Route Success: %s to %s via [", STATION_NAME, response->destination);

    for (int i = 0; i < response->visited_count; i++) {
        char segment[MAX_JSON_BUFFER];
        snprintf(segment, sizeof(segment), "%s (Depart: %s, Arrive: %s, Service: %s) ",
                 response->visited[i].station_name,
                 response->visited[i].arrival_time,
                 response->visited[i].departure_time,
                 response->visited[i].service_name);

        // Concatenate the segment to the route message
        strncat(route_message, segment, size - strlen(route_message) - 1);
    }

    strncat(route_message, "]", size - strlen(route_message) - 1);
    snprintf(route_message + strlen(route_message), size - strlen(route_message), 
             ", departing at %s and arriving at time %s", 
             response->original_time, response->arrival_time);
}

// --------------------------------------------------------- APPEND TO VISITED STRUCT ---------------------------------------------------------

void append_to_visited(RoutingRequest *request, const char *station_name, const char *arrival_time, const char *departure_time, const char *service_name) {
    if (request->visited_count >= MAX_ENTRIES) {
        fprintf(stderr, "Visited array full, cannot append station %s\n", station_name);
        return;
    }
    strncpy(request->visited[request->visited_count].station_name, station_name, MAX_NAME_LENGTH);
    strncpy(request->visited[request->visited_count].arrival_time, arrival_time, 6);
    strncpy(request->visited[request->visited_count].departure_time, departure_time, 6);
    strncpy(request->visited[request->visited_count].service_name, service_name, MAX_NAME_LENGTH);
    request->visited_count++;
}

void append_to_visitedR(RoutingResponse *request, const char *station_name, const char *arrival_time, const char *departure_time, const char *service_name) {
    if (request->visited_count >= MAX_ENTRIES) {
        fprintf(stderr, "Visited array full, cannot append station %s\n", station_name);
        return;
    }
    strncpy(request->visited[request->visited_count].station_name, station_name, MAX_NAME_LENGTH);
    strncpy(request->visited[request->visited_count].arrival_time, arrival_time, 6);
    strncpy(request->visited[request->visited_count].departure_time, departure_time, 6);
    strncpy(request->visited[request->visited_count].service_name, service_name, MAX_NAME_LENGTH);
    request->visited_count++;
}

// --------------------------------------------------------- JSON DESERIALIZE/SERIALIZE ---------------------------------------------------------

void deserialize_visited_station(const char *data, VisitedStation *vstation) {
    extract_json_value(vstation->station_name, sizeof(vstation->station_name), data, "\"station_name\":");
    extract_json_value(vstation->arrival_time, sizeof(vstation->arrival_time), data, "\"arrival_time\":");
    extract_json_value(vstation->departure_time, sizeof(vstation->departure_time), data, "\"departure_time\":");
    extract_json_value(vstation->service_name, sizeof(vstation->service_name), data, "\"service_name\":");
}

void deserialize_routing_request(const char *data, RoutingRequest *request) {
    extract_json_value(request->destination, sizeof(request->destination), data, "\"destination\":");
    extract_json_value(request->time, sizeof(request->time), data, "\"time\":");
    extract_json_value(request->original_departure_time, sizeof(request->original_departure_time), data, "\"original_departure_time\":");
    extract_json_value(request->original_service, sizeof(request->original_service), data, "\"original_service\":");
    extract_json_value(request->request_id, sizeof(request->request_id), data, "\"request_id\":");

    const char *visited_start = strstr(data, "\"visited\": [");
    if (visited_start) {
        visited_start += strlen("\"visited\": [");
        const char *visited_end = strchr(visited_start, ']');
        if (visited_end) {
            char visited_buffer[MAX_JSON_BUFFER];
            size_t visited_len = (size_t)(visited_end - visited_start);
            strncpy(visited_buffer, visited_start, visited_len);
            visited_buffer[visited_len] = '\0';

            request->visited_count = 0;
            const char *entry_start = visited_buffer;
            while ((entry_start = strchr(entry_start, '{')) != NULL) {
                const char *entry_end = strchr(entry_start, '}');
                if (entry_end) {
                    char entry_buffer[256];
                    size_t entry_len = (size_t)(entry_end - entry_start + 1);
                    strncpy(entry_buffer, entry_start, entry_len);
                    entry_buffer[entry_len] = '\0';

                    deserialize_visited_station(entry_buffer, &request->visited[request->visited_count++]);
                    entry_start = entry_end + 1;
                } else {
                    break;
                }
            }
        }
    }
}

void serialize_routing_response(const RoutingResponse *response, char *buffer, size_t buf_size) {
    char visited_buffer[MAX_JSON_BUFFER] = "[";
    for (int i = 0; i < response->visited_count; i++) {
        char entry_buffer[256];
        snprintf(entry_buffer, sizeof(entry_buffer),
                 "{\"station_name\":\"%s\",\"arrival_time\":\"%s\",\"departure_time\":\"%s\",\"service_name\":\"%s\"}",
                 response->visited[i].station_name,
                 response->visited[i].arrival_time,
                 response->visited[i].departure_time,
                 response->visited[i].service_name);
        strcat(visited_buffer, entry_buffer);
        if (i < response->visited_count - 1) {
            strcat(visited_buffer, ",");
        }
    }
    strcat(visited_buffer, "]");

    snprintf(buffer, buf_size,
             "{\"departure_time\": \"%s\",\"type\": \"%s\",\"arrival_time\": \"%s\",\"destination\": \"%s\",\"original_time\": \"%s\",\"leaving_service\": \"%s\",\"route\": %s,\"request_id\": \"%s\",\"visited_index\": %d}",
             response->depature_time,
             response->type,
             response->arrival_time,
             response->destination,
             response->original_time,
             response->leaving_service,
             visited_buffer,
             response->request_id,
             response->visited_index);
}

void deserialize_routing_response(const char *data, RoutingResponse *response) {
    extract_json_value(response->depature_time, sizeof(response->depature_time), data, "\"departure_time\":");
    extract_json_value(response->type, sizeof(response->depature_time), data, "\"type\":");
    extract_json_value(response->arrival_time, sizeof(response->arrival_time), data, "\"arrival_time\":");
    extract_json_value(response->destination, sizeof(response->destination), data, "\"destination\":");
    extract_json_value(response->original_time, sizeof(response->original_time), data, "\"original_time\":");
    extract_json_value(response->leaving_service, sizeof(response->leaving_service), data, "\"leaving_service\":");
    extract_json_value(response->request_id, sizeof(response->request_id), data, "\"request_id\":");

    char visited_index_str[16];
    extract_json_value(visited_index_str, sizeof(visited_index_str), data, "\"visited_index\":");
    response->visited_index = atoi(visited_index_str);

    const char *visited_start = strstr(data, "\"route\": [");
    if (visited_start) {
        visited_start += strlen("\"route\": [");
        const char *visited_end = strchr(visited_start, ']');
        if (visited_end) {
            char visited_buffer[MAX_JSON_BUFFER];
            size_t visited_len = (size_t)(visited_end - visited_start);
            strncpy(visited_buffer, visited_start, visited_len);
            visited_buffer[visited_len] = '\0';

            response->visited_count = 0;
            const char *entry_start = visited_buffer;
            while ((entry_start = strchr(entry_start, '{')) != NULL) {
                const char *entry_end = strchr(entry_start, '}');
                if (entry_end) {
                    char entry_buffer[256];
                    size_t entry_len = (size_t)(entry_end - entry_start + 1);
                    strncpy(entry_buffer, entry_start, entry_len);
                    entry_buffer[entry_len] = '\0';

                    deserialize_visited_station(entry_buffer, &response->visited[response->visited_count++]);
                    entry_start = entry_end + 1;
                } else {
                    break;
                }
            }
        }
    }
}

void serialize_routing_request(const RoutingRequest *request, char *buffer, size_t buf_size) {
    char visited_buffer[MAX_JSON_BUFFER] = "[";
    for (int i = 0; i < request->visited_count; i++) {
        char entry_buffer[256];
        snprintf(entry_buffer, sizeof(entry_buffer),
                 "{\"station_name\": \"%s\",\"arrival_time\": \"%s\",\"departure_time\": \"%s\",\"service_name\": \"%s\"}",
                 request->visited[i].station_name,
                 request->visited[i].arrival_time,
                 request->visited[i].departure_time,
                 request->visited[i].service_name);
        strcat(visited_buffer, entry_buffer);
        if (i < request->visited_count - 1) {
            strcat(visited_buffer, ",");
        }
    }
    strcat(visited_buffer, "]");

    snprintf(buffer, buf_size,
             "{\"destination\": \"%s\",\"time\": \"%s\",\"visited\": %s,\"original_departure_time\": \"%s\",\"original_service\": \"%s\",\"request_id\": \"%s\"}",
             request->destination,
             request->time,
             visited_buffer,
             request->original_departure_time,
             request->original_service,
             request->request_id);
}

// Function to extract value from a JSON-like string by key
void extract_json_value(char *result, size_t max_len, const char *data, const char *key) {
    const char *start = strstr(data, key);
    if (start) {
        start += strlen(key); // Move to the end of the key
        while (*start && (*start == ' ' || *start == ':' || *start == '=')) start++; // Skip spaces, colon, or equals sign

        // Check if the value starts with a quote or a bracket (for nested JSON)
        int is_quoted = 0;
        int is_nested = 0;
        if (*start == '\"') {
            is_quoted = 1;
            start++; // Skip the starting quote
        } else if (*start == '{' || *start == '[') {
            is_nested = 1;
        }

        // Handle nested JSON or regular values
        const char *end = start;
        if (is_quoted) {
            // Find the closing quote for quoted values
            while (*end && *end != '\"') end++;
        } else if (is_nested) {
            // Find the end of the nested structure
            int nested = 0;
            while (*end) {
                if (*end == '{' || *end == '[') nested++;
                if (*end == '}' || *end == ']') nested--;
                end++;
                if (nested == 0) break;
            }
        } else {
            // Find the end of the value for non-quoted values
            while (*end && *end != ',' && *end != '}' && *end != ']') end++;
        }

        size_t len = (size_t)(end - start);
        if (len > 0 && len < max_len) {
            strncpy(result, start, len);
            result[len] = '\0'; // Ensure null-termination
        } else if (max_len > 0) {
            result[0] = '\0'; // Provide an empty string if the length is zero or too large
        }
    } else if (max_len > 0) {
        result[0] = '\0'; // Ensure the result is always a valid string
    }
}



// Example function to deserialize data into our packet struct
void deserialize_udp_packet(char *data, UDP_Packet *packet) {
    extract_json_value(packet->type, sizeof(packet->type), data, "\"type\":");
    extract_json_value(packet->originStationName, sizeof(packet->originStationName), data, "\"originStationName\":");
    extract_json_value(packet->packetID, sizeof(packet->packetID), data, "\"packetID\":");
    extract_json_value(packet->msg, sizeof(packet->msg), data, "\"msg\":");
}

// --------------------------------------------------------- PINGING NEIGHBOURS ---------------------------------------------------------

void pingNeighbors(bool all, bool inactive) {
    if (all) {
        // Reset active neighbors
        memset(active_neighbors, 0, sizeof(active_neighbors));
        active_neighbor_count = 0;

        for (int i = 0; i < NEIGHBOR_COUNT; i++) {
            char *neighbor_addr = NEIGHBORS[i];
            char ip[100];
            int port;
            sscanf(neighbor_addr, "%99[^:]:%d", ip, &port);
            UDP_sendMessage(ip, port, PING, NULL, NULL, -1);
        }
    } else if (inactive) {
        for (int i = 0; i < NEIGHBOR_COUNT; i++) {
            char *neighbor_addr = NEIGHBORS[i];
            char ip[MAX_IP_LEGNTH];
            int port;
            sscanf(neighbor_addr, "%99[^:]:%d", ip, &port);

            bool isActive = false;
            for (int j = 0; j < active_neighbor_count; j++) {
                if (htons((uint16_t)active_neighbors[j].port) == htons((uint16_t)port) &&
                    inet_addr(active_neighbors[j].ip) == inet_addr(ip)) {
                    isActive = true;
                    break;
                }
            }

            if (!isActive) {
                UDP_sendMessage(ip, port, PING, NULL, NULL, -1);
            }
        }
    }
}


// Simple function to generate a "unique" packet ID
void generate_unqiue_id(char *id, size_t size) {
    static int counter = 0;
    snprintf(id, size, "%d-%s", counter++, STATION_NAME);  // Simple counter based ID for demonstration
}


// Simplified JSON-like packet creation
void create_packet(char *buffer, const char *type, const char *stationName, const char *packetID, const char *msg) {
    if (msg && msg[0] != '\0') {
        // If msg is provided and not empty, include it without quotes
        snprintf(buffer, MAX_JSON_BUFFER, "{\"type\": \"%s\",\"originStationName\": \"%s\",\"packetID\": \"%s\",\"msg\": %s}",
                 type, stationName, packetID, msg);
    } else {
        // If msg is empty or NULL, include "None" with quotes
        snprintf(buffer, MAX_JSON_BUFFER, "{\"type\": \"%s\",\"originStationName\": \"%s\",\"packetID\": \"%s\",\"msg\": \"None\"}",
                 type, stationName, packetID);
    }
}

// --------------------------------------------------------- UDP SEND MESSAGE ---------------------------------------------------------

void UDP_sendMessage(const char *ip, int port, const char *packetType, const char *msg, const char *ACK_responseID, int resendIndex) {
    if (!ip || port == 0 || !packetType) {
        ERROR_PRINT("Invalid parameters for UDP message.");
        return;
    }

    struct sockaddr_in targetAddress;
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &targetAddress.sin_addr);

    char packetID[MAX_ID_LENGTH];
    if (strcmp(packetType, ACK) == 0 && ACK_responseID) {
        strncpy(packetID, ACK_responseID, sizeof(packetID));
    } else {
        generate_unqiue_id(packetID, sizeof(packetID));
    }

    char packet[MAX_JSON_BUFFER];
    if (resendIndex < 0) {
        create_packet(packet, packetType, STATION_NAME, packetID, msg);
    } else {
        strcpy(packet, sent_messages[resendIndex].packet);  // Resend the existing packet
    }

    // Log and send the message
    printf("%s --> Sending to IP: %s, Port: %d, Type: %s\n",STATION_NAME, ip, port, packetType);
    ssize_t bytes_sent = sendto(UDP_SOCKET, packet, strlen(packet), 0, (struct sockaddr *)&targetAddress, sizeof(targetAddress));
    if (bytes_sent < 0) {
        perror("Failed to send UDP packet");
    }

    // Store sent message if not ACK
    if (strcmp(packetType, ACK) != 0) {
        struct sent_message *sm = &sent_messages[sent_messages_count++];
        strcpy(sm->packetID, packetID);
        strcpy(sm->packetType, packetType);
        sm->time_latestSent = time(NULL);
        sm->attempts = 1;
        sm->targetAddress = targetAddress;
        strcpy(sm->packet, packet);
    }
}

// --------------------------------------------------------- SET UP SOCKETS ---------------------------------------------------------

void setup_sockets() {
    if (TCP_PORT == 0 || UDP_PORT == 0) {
        fprintf(stderr, "TCP_PORT or UDP_PORT not set correctly.\n");
        exit(EXIT_FAILURE);
    }

    // Create TCP socket
    TCP_SOCKET = socket(AF_INET, SOCK_STREAM, 0);
    if (TCP_SOCKET < 0) {
        perror("Failed to create TCP socket");
        exit(EXIT_FAILURE);
    }

    // Set TCP socket options
    int opt = 1;
    if (setsockopt(TCP_SOCKET, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set TCP socket options");
        close(TCP_SOCKET);
        exit(EXIT_FAILURE);
    }

    // Bind TCP socket
    struct sockaddr_in tcp_addr;
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons((uint16_t)TCP_PORT);
    if (bind(TCP_SOCKET, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("Failed to bind TCP socket");
        close(TCP_SOCKET);
        exit(EXIT_FAILURE);
    }

    // Listen on TCP socket
    if (listen(TCP_SOCKET, TCP_BACKLOG_LENGTH) < 0) {
        perror("Failed to listen on TCP socket");
        close(TCP_SOCKET);
        exit(EXIT_FAILURE);
    }

    // Create UDP socket
    UDP_SOCKET = socket(AF_INET, SOCK_DGRAM, 0);
    if (UDP_SOCKET < 0) {
        perror("Failed to create UDP socket");
        close(TCP_SOCKET); // Clean up TCP socket before exit
        exit(EXIT_FAILURE);
    }

    // Bind UDP socket
    struct sockaddr_in udp_addr;
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons((uint16_t)UDP_PORT);
    if (bind(UDP_SOCKET, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("Failed to bind UDP socket");
        close(TCP_SOCKET); // Clean up TCP socket
        close(UDP_SOCKET); // Clean up UDP socket
        exit(EXIT_FAILURE);
    }

    // Set UDP socket to non-blocking mode
    int flags = fcntl(UDP_SOCKET, F_GETFL, 0);
    if (fcntl(UDP_SOCKET, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set UDP socket to non-blocking mode");
        close(TCP_SOCKET);
        close(UDP_SOCKET);
        exit(EXIT_FAILURE);
    }
}

// --------------------------------------------------------- HANDLE CLIENT CONNECTION ---------------------------------------------------------

void handle_client_connection(int tcp_socket) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    ssize_t recv_len = recv(tcp_socket, buffer, sizeof(buffer) - 1, 0);
    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            perror("Error receiving data");
            return;
        }
    } else if (recv_len == 0) {
        return;
    }

    buffer[recv_len] = '\0';

    char *method = strtok(buffer, " ");
    char *path = strtok(NULL, " ");
    char *version = strtok(NULL, "\r\n");

    if (!method || !path || !version) {
        return;
    }

    if (strcmp(method, "GET") == 0 && strstr(path, "to=")) {
        char *query = strchr(path, '?');
        if (query) {
            char *selected_destination = NULL;
            char *leave_time = NULL;
            query++;
            char *token = strtok(query, "&");
            while (token) {
                if (strncmp(token, "to=", 3) == 0) {
                    selected_destination = token + 3;
                } else if (strncmp(token, "leave=", 6) == 0) {
                    leave_time = token + 6;
                }
                token = strtok(NULL, "&");
            }

            if (selected_destination && leave_time) {
                // Decode URL-encoded characters (e.g., %3A -> :) (Used to extract the time from the website)
                char decoded_leave_time[16];
                decode_url(decoded_leave_time, leave_time);

                bool destination_found = false;
                for (int i = 0; i < station.num_entries; ++i) {
                    if (strcmp(station.entries[i].arrival_station, selected_destination) == 0) {
                        destination_found = true;
                        break;
                    }
                }

                if (destination_found) {
                    TimetableEntry *departure = find_direct_or_query_indirect(decoded_leave_time, selected_destination, tcp_socket);
                    if (departure) {
                        char response_message[512];
                        snprintf(response_message, sizeof(response_message),
                                "Depart at %s from %s, arrive by %s at %s.",
                                departure->departure_time, departure->departure_station,
                                departure->arrival_time, departure->arrival_station);
                        TCP_sendMessage(tcp_socket, response_message);
                        return;
                    } else {
                        char response_message[512];
                        snprintf(response_message, sizeof(response_message),
                                "There is no journey from %s to %s leaving after %s.",
                                STATION_NAME, selected_destination, decoded_leave_time);
                        TCP_sendMessage(tcp_socket, response_message);
                    }
                } else if (is_time_after_last_departure(decoded_leave_time)) {
                    char response_message[512];
                    snprintf(response_message, sizeof(response_message),
                             "There is no journey from %s to %s leaving after %s.",
                             STATION_NAME, selected_destination, decoded_leave_time);
                    TCP_sendMessage(tcp_socket, response_message);
                } else {
                    find_direct_or_query_indirect(decoded_leave_time, selected_destination, tcp_socket);
                }
            }
        }
    }
    return;
}

// This function extracts the chosen time by the user from the website. 
void decode_url(char *dst, const char *src) {
    while (*src) {
        if (*src == '%') {
            if (src[1] && src[2]) {
                char hex[3];
                hex[0] = src[1];
                hex[1] = src[2];
                hex[2] = '\0';
                *dst++ = (char)strtol(hex, NULL, 16);
                src += 2;
            }
        } else if (*src == '+') {
            *dst++ = ' ';
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

bool is_time_after_last_departure(const char *selected_time) {
    int selected_minutes = time_to_minutes(selected_time);
    int last_departure_minutes = time_to_minutes(station.entries[station.num_entries - 1].departure_time);
    return selected_minutes > last_departure_minutes;
}

// --------------------------------------------------------- SEND TCP MESSAGE ---------------------------------------------------------

void TCP_sendMessage(int sock, const char *msg) {
    // Set socket to non-blocking to check if it's still open
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    char buffer[16];
    ssize_t recv_status = recv(sock, buffer, sizeof(buffer), 0);

    if (recv_status == 0) {
        // Connection closed by client
        ERROR_PRINT("Socket connection closed by the client.");
        close(sock);
        return;
    } else if (recv_status < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Real error occurred
        perror("Recv failed");
        close(sock);
        return;
    }

    // Prepare HTML content
    char response_html[MAX_JSON_BUFFER];
    snprintf(response_html, sizeof(response_html),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n\r\n"
             "<html>\n"
             "<body>\n"
             "   <h1>Travel Plan</h1>\n"
             "   <p>%s</p>\n"
             "   </p><a href='#'' onclick='window.history.back(); return false;'>Return</a>\n"
             "</body>\n"
             "</html>\n", msg);

    // Send the response
    if (send(sock, response_html, strlen(response_html), 0) < 0) {
        perror("Send failed");
        close(sock);
        return;
    }

    // Close the connection
    close(sock);
    DEBUG_PRINT("Sent TCP packet with message");
}


// Function to find the next departure
TimetableEntry *find_next_departure(StationTimetable *timetable, const char *target_time_str, const char *destination) {
    int target_time = time_to_minutes(target_time_str);
    int min_difference = INT_MAX;
    TimetableEntry *nearest_departure = NULL;

    for (int i = 0; i < timetable->num_entries; i++) {
        TimetableEntry *entry = &timetable->entries[i];
        if (strcmp(entry->arrival_station, destination) == 0) {
            int departure_time = time_to_minutes(entry->departure_time);
            int time_difference = departure_time - target_time;
            if (time_difference >= 0 && time_difference < min_difference) {
                min_difference = time_difference;
                nearest_departure = entry;
            }
        }
    }

    return nearest_departure;
}


TimetableEntry *find_direct_or_query_indirect(const char *selected_time, const char *selected_destination, int tcp_socket) {
    TimetableEntry *departure = find_next_departure(&station, selected_time, selected_destination);
    if (departure) {
        return departure;  // Found a direct departure
    }

    char request_id[MAX_ID_LENGTH];
    generate_unique_id(request_id, sizeof(request_id)); // Generate a unique request ID

    RoutingRequest message;
    strcpy(message.destination, selected_destination);
    strcpy(message.time, selected_time);  // Assuming selected_time is formatted as "HH:MM"
    strcpy(message.request_id, request_id);
    message.original_departure_time[0] = '\0';  // Set to NULL
    message.original_service[0] = '\0';  // Set to NULL
    message.visited_count = 0;
    struct active_routingRequest *request = &active_routingRequests[active_routingRequests_count];
    strcpy(request->destination, selected_destination);
    request->time_sent = time(NULL);
    request->socket = tcp_socket;
    strcpy(request->request_id, request_id);
    memset(request->responses, 0, sizeof(request->responses));

    active_routingRequests_count++;
    printf("%s --> Requests Count after ++: %i\n",STATION_NAME, active_routingRequests_count);
    query_neighbors(&message);

    return NULL;
}

void generate_unique_id(char *buffer, size_t size) {
    static unsigned long counter = 0;  // static counter to ensure uniqueness between calls
    time_t now = time(NULL);  // get current time for a unique part of the ID

    // Generate the ID using snprintf for safe string formatting
    snprintf(buffer, size, "%lu-%ld", counter++, now); // ASSUME NO TO PACKETS ARE GENERATED AT THE EXACT POINT IN TIME (HIGHLY UNLIKELY)
}

void query_neighbors(RoutingRequest *message) {
    VERBOSE_PRINT("Querying neighbors...");

    char debug_msg[256];
    snprintf(debug_msg, sizeof(debug_msg), "Failed to find route: %s ---> %s. Querying neighbors...", STATION_NAME, message->destination);
    VERBOSE_PRINT(debug_msg);
    
    printf("%s --> Active Neighbours: %d, Visited Count: %d\n", STATION_NAME, active_neighbor_count, message->visited_count);
    for (int i = 0; i < active_neighbor_count; i++) {
        NeighborInfo neighbor = active_neighbors[i];
        bool neighbor_visited = false;

        for (int j = 0; j < message->visited_count; j++) {
            if (strcmp(message->visited[j].station_name, neighbor.stationName) == 0) {
                neighbor_visited = true;
                break;
            }
        }

        // Check if this neighbor has already been visited
        if (!neighbor_visited) {
            TimetableEntry *next_departure = find_next_departure(&station, message->time, neighbor.stationName);
            if (next_departure) {
                RoutingRequest message_temp = *message;
                
                append_to_visited(&message_temp, STATION_NAME, next_departure->departure_time, next_departure->arrival_time, next_departure->service_name);

                if (strlen(message_temp.original_departure_time) == 0) {
                    strcpy(message_temp.original_departure_time, next_departure->departure_time);
                }
                
                strcpy(message_temp.time, next_departure->arrival_time); // Ensures that it uses this time when calculating the depture in neighbour stations
                strcpy(message_temp.original_service, next_departure->service_name);
                

                // Serialize the message
                char serialized_data[MAX_JSON_BUFFER];
                serialize_routing_request(&message_temp, serialized_data, sizeof(serialized_data));
                // Send the UDP message
                UDP_sendMessage(neighbor.ip, neighbor.port, ROUTE_QUERY, serialized_data, NULL, -1);
            } else {
                snprintf(debug_msg, sizeof(debug_msg), "No available departures to neighbor %s, skipping.", neighbor.stationName);
                DEBUG_PRINT(debug_msg);
            }
        } else {
            snprintf(debug_msg, sizeof(debug_msg), "Skipping %s as it has already been visited.", neighbor.stationName);
            DEBUG_PRINT(debug_msg);
            neighbor_visited = false;
        }
    }
}

// --------------------------------------------------------- LOAD TIMETABLE ---------------------------------------------------------

void init_station_timetable(StationTimetable *timetable, const char *station_name) {
    strncpy(timetable->station_name, station_name, sizeof(timetable->station_name) - 1);
    timetable->num_entries = 0;
    snprintf(timetable->timetable_file, sizeof(timetable->timetable_file), "tt-%s", station_name);
    timetable->location[0] = -1;
    timetable->location[1] = -1;
    load_timetable(timetable);
}


void load_timetable(StationTimetable *timetable) {
    FILE *file = fopen(timetable->timetable_file, "r");
    if (!file) {
        printf("Timetable file %s not found.\n", timetable->timetable_file);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '#' || line[0] == '\n') continue; // Skip comments and empty lines

        TimetableEntry entry;
        float x, y; // Use float for coordinates
        // Try parsing as a header first
        if (sscanf(line, "%31[^,],%f,%f", entry.departure_station, &x, &y) == 3) {
            if (strcmp(entry.departure_station, timetable->station_name) == 0) {
                timetable->location[0] = x;
                timetable->location[1] = y;
            } else {
                printf("Incorrect header for station %s: %s\n", timetable->station_name, line);
            }
        } else if (sscanf(line, "%15[^,],%31[^,],%31[^,],%15[^,],%31s",
                          entry.departure_time, entry.service_name, entry.departure_station,
                          entry.arrival_time, entry.arrival_station) == 5) {
            if (timetable->num_entries < MAX_ENTRIES) {
                timetable->entries[timetable->num_entries++] = entry;
            }
        } else {
            printf("Incorrectly formed timetable entry: %s\n", line);
        }
    }

    fclose(file);

    // Update mod-time
    timetable->mod_time = get_mod_time(timetable->timetable_file);
}


// Check if the timetable file needs updating
void update_timetable(StationTimetable *timetable) {
  if (get_mod_time(timetable->timetable_file) > timetable->mod_time) {
    load_timetable(timetable);
    printf("Updated timetable file");
  }
}


// Helper functions
void print_active_neighbors(void) {
    printf("Active Neighbors:\n");
    for (int i = 0; i < active_neighbor_count; i++) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(active_neighbors[i].ip), ip, INET_ADDRSTRLEN);
        int port = ntohs((uint16_t)active_neighbors[i].port);
        printf("Neighbor %d: IP: %s, Port: %d\n", i + 1, ip, port);
    }
}


// Get the modification time of a file
time_t get_mod_time(const char *filename) {
    struct stat file_stat;
    if (stat(filename, &file_stat) == -1) {
        perror("Failed to get file status");
        return (time_t)-1;
    }
    return file_stat.st_mtime;
}