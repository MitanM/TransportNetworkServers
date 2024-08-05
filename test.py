import sys
import socket
import select
import urllib.parse
from datetime import datetime, timedelta
import json
import time
import uuid
import copy
import os

# ============================== GROUP MEMBERS ==============================
#                           Akhil Gorasia (23424609)
#                          Cameron O'Neill (23340022)
#                          Martin Mitanoski (23385544)
# ===========================================================================

# Global 
COMMENT = '#'                   # Charcter used for comments in files
DEBUG = True                   # Error reporting
VERBOSE = True                  # Log reporting
TCP_BACKLOG_LENGTH = 5          # Maximum connections to hold on TCP socket
UDP_MAX_SEND_ATTEMPTS = 3       # Maximum possible attempts at sending a packet over UDP
UDP_ROUND_TRIP_TIME = 10         # Maximum latency from one end of the network to the other (in seconds)

############################################################
# WARNING: If using in adjacent with C station,            #
# then ROUTE_RESPONSE_TIME needs to be much greater (~30s) #
############################################################

ROUTE_RESPONSE_TIME = 25         # Time to wait before closing an active route

# Helper functions
DEBUG_PRINT = lambda msg: print(f"[DEBUG{f' {STATION_IDENTIFIER}'}] {msg}" if STATION_IDENTIFIER else f"[DEBUG] {msg}") if DEBUG else None
VERBOSE_PRINT = lambda msg: print(f"[INFO{f' {STATION_IDENTIFIER}'}] {msg}" if STATION_IDENTIFIER else f"[INFO] {msg}") if VERBOSE else None
ERROR_PRINT = lambda msg: print(f"[ERROR{f' {STATION_IDENTIFIER}'}] {msg}" if STATION_IDENTIFIER else f"[ERROR] {msg}")
def VERBOSE_PRINT_EMPTY(): print("") if VERBOSE else None

#Global Variables
TCP_SOCKET = None
TCP_PORT = None
UDP_SOCKET = None
UDP_PORT = None
STATION = None              # StationTimetable for current station
STATION_IDENTIFIER = None   # Single identifying letter taken from the station's name (last character)
STATION_NAME = None
NEIGHBORS = None            # All possible neighbors for current station (active/inactive)

# Global dictionaries
active_neighbors = {}       # Store active neighbors (responded to PING packet)
sent_messages = {}          # Store sent packets (to be able to resend if no ACK)
active_routingRequests = {} # Existing routing requests from this station

# Packet Types
ACK = 'ACK'
PING = 'PING'
ROUTE_SUCCESS = 'ROUTE_SUCCESS'
ROUTE_QUERY = "ROUTE_QUERY"
ROUTE_FAIL = "ROUTE_FAIL"



# --------------------- Helper functions ---------------------
# Parse arguments passed to program
def parse_arguments():
    global STATION_IDENTIFIER, STATION_NAME, TCP_PORT, UDP_PORT, NEIGHBORS
    if len(sys.argv) < 5:
        ERROR_PRINT("Usage: python3 station-server.py <Station-Name> <TCP-Port> <UDP-Port> <Neighbor1> ...")
        sys.exit(1)

    STATION_NAME = sys.argv[1]
    TCP_PORT = int(sys.argv[2])
    UDP_PORT = int(sys.argv[3])
    NEIGHBORS = sys.argv[4:]
    STATION_IDENTIFIER = STATION_NAME[-1]


# Get a file's mod-time (to check if a file has changed)
def get_mod_time(filename):
    try:
        return os.path.getmtime(filename)
    except Exception as e:
        ERROR_PRINT(f"Could not access modification time of {filename}. Error: {e}")
        return None



# --------------------- Socket Setup ---------------------
def setup_sockets():
    if TCP_PORT is None or UDP_PORT is None:
        ERROR_PRINT("TCP_PORT or UDP_PORT not set correctly.")
        sys.exit(1)

    global TCP_SOCKET, UDP_SOCKET
    TCP_SOCKET = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    TCP_SOCKET.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    TCP_SOCKET.bind(('0.0.0.0', TCP_PORT))
    TCP_SOCKET.listen(TCP_BACKLOG_LENGTH)

    UDP_SOCKET = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    UDP_SOCKET.bind(('0.0.0.0', UDP_PORT))


# --------------------- Timetable Class ---------------------
class StationTimetable:
    def __init__(self, station_name):
        self.station_name = station_name
        self.timetable = []
        self.timetable_file = f"tt-{station_name}"
        self.location = 0, 0
        self.mod_time = None
        self.load_timetable()

    def load_timetable(self):
        """Read the timetable file and load it into the timetable list."""
        try:
            with open(self.timetable_file, 'r') as file:
                for line in file:
                    # Skip if comment
                    if line.strip().startswith(COMMENT):
                        continue
                    parts = line.strip().split(',')

                    # For timetable entry
                    if len(parts) == 5:
                        entry = {
                            'departure_time': parts[0],
                            'service_name': parts[1],
                            'departure_station': parts[2],
                            'arrival_time': parts[3],
                            'arrival_station': parts[4]
                        }
                        self.timetable.append(entry)

                    # For header
                    elif len(parts) == 3:
                        if parts[0] == self.station_name:
                            self.location = (parts[1], parts[2])
                        else:
                            ERROR_PRINT(f"Incorrect header for station {self.station_name}: '{line}'")

                    # Incorrectly formed timetable entry
                    else:
                        ERROR_PRINT(f"Incorrect timetable entry for {self.station_name}: '{line}'")

            # Update mod-time of file
            self.mod_time = get_mod_time(self.timetable_file)

        except FileNotFoundError:
            ERROR_PRINT(f"Timetable file {self.timetable_file} not found.")
        except Exception as e:
            ERROR_PRINT(f"Failed to load timetable ({self.timetable_file}): {str(e)}")

    def update_timetable(self):
        # Check if mod time has changed (if its None - it couldnt be read initially, so dont update)
        if self.mod_time is not None and (get_mod_time(self.timetable_file) > self.mod_time):
            self.load_timetable()
            VERBOSE_PRINT("Updated timetable! (Modification dedected)")

    def get_stationName(self):
        return self.station_name

    def get_destinations(self):
        destinations = set()
        for entry in self.timetable:
            destinations.add(entry['arrival_station'])
        return sorted(destinations)

    def get_timetable(self):
        """Return the loaded timetable."""
        return self.timetable

    def find_next_departure(self, target_time_str, destination):
        """Finds the nearest departure time to the provided time for a given destination."""
        target_time = datetime.strptime(target_time_str, '%H:%M')
        min_difference = timedelta(days=1)  # Arbitrary large timedelta
        nearest_departure = None
        for entry in self.timetable:
            if entry['arrival_station'] == destination:
                departure_time = datetime.strptime(entry['departure_time'], '%H:%M')
                time_difference = departure_time - target_time if departure_time >= target_time else timedelta(days=1)
                if time_difference < min_difference:
                    min_difference = time_difference
                    nearest_departure = entry
        return nearest_departure if nearest_departure else None

    def is_time_after_last_departure(self, selected_time_str):
        """Check if the selected_time is after the last departure in the timetable."""
        last_departure_time_str = self.timetable[-1]['departure_time']
        selected_time = datetime.strptime(selected_time_str, '%H:%M')
        last_departure_time = datetime.strptime(last_departure_time_str, '%H:%M')

        return selected_time > last_departure_time



# --------------------- Find Route Handling ---------------------
def query_neighbors(message):
    global sent_messages
    current_station = {
        "station_name": STATION_NAME,
        "arrival_time": message['time'],
        "departure_time": None,  # to be updated once the next departure is known
        "service_name": None     # to be updated once the next departure is known
    }
    
    VERBOSE_PRINT(f"Failed to find route: {STATION_NAME} ---> {message['destination']}. Querying neighbors...")

    current_time_str = message['time']
    DEBUG_PRINT(f"Current time for forwarding message: {current_time_str}")
    DEBUG_PRINT(f"Current neighbours: {active_neighbors}")
    for neighbor_stationName, address in active_neighbors.items():
        # Ensure that each neighbor station doesnt affect each other's original message and visited list
        message_temp = copy.deepcopy(message) # Ensure no references are modified
        visited = message_temp.get('visited', [])

        if neighbor_stationName not in [visit['station_name'] for visit in visited]:
            next_departure = STATION.find_next_departure(current_time_str, neighbor_stationName)
            message_temp = set_original_time_and_service(current_time_str, neighbor_stationName, message_temp)

            if next_departure:
                current_station["departure_time"] = next_departure['departure_time']
                current_station["service_name"] = next_departure['service_name']
                visited.append(current_station)
                message_temp['time'] = next_departure['arrival_time']
                message_temp['visited'] =  visited
                UDP_sendMessage(targetAddress=address, targetStationName=neighbor_stationName, packetType=ROUTE_QUERY, msg=message_temp)
            else:
                DEBUG_PRINT(f"No available departues to neighbor {neighbor_stationName}, skipping.")
                VERBOSE_PRINT(f"Unable to reach destination in time. Sending route failure back")

                response = {
                    "type": "fail",
                    "departure_time": None,
                    "arrival_time": None,
                    "destination": message['destination'],
                    "original_time": message["original_departure_time"],
                    "leaving_service": None,
                    "route": message['visited'],
                    "request_id": message["request_id"],
                    "visited_index": (len(visited) - 1) # Remove 1 to adjust for indexing at 0
                }
                backtrack_station_name = response['route'][response['visited_index']]['station_name']
                backTrack(route_outcome=ROUTE_FAIL, backtrack_station_name=backtrack_station_name, msg_toSend=response)

        else:
            DEBUG_PRINT(f"Skipping {neighbor_stationName} as it has already been visited.")



def find_direct_or_query_indirect(selected_time, selected_destination, TCP_clientConnection):
    global active_routingRequests

    departure = STATION.find_next_departure(selected_time, selected_destination)

    if selected_destination in STATION.get_destinations():
        return departure

    if departure:
        return departure
     
    if (STATION.is_time_after_last_departure(selected_time)):
        return None

    # Add routing request to list
    request_id = str(uuid.uuid4()) + STATION_NAME # Ensure no other station can have conflicting unique request IDs
    active_routingRequests[request_id] = {
        "destination": selected_destination,
        "responses": [],
        "time_sent": datetime.now(),
        "socket": TCP_clientConnection 
    }

    # Query neighbors to see if they have a valid path
    message = {
        "destination": selected_destination,
        "time": selected_time,
                                                                                                                   
        "visited": [],  # Initialize visited list
        "original_departure_time": None,
        "original_service": None,
        "request_id": request_id
    }
    query_neighbors(message)
    
    return None



def set_original_time_and_service(time, neighbor, message):
    message_copy = message
    if (message_copy["original_departure_time"] is None):
        next_departure = STATION.find_next_departure(time, neighbor)
        message_copy["original_departure_time"] = next_departure["departure_time"]
        message_copy["original_service"] = next_departure["service_name"]
    
    return message_copy


def backTrack(route_outcome, backtrack_station_name, msg_toSend):
    backtrack_addr = active_neighbors.get(backtrack_station_name, None)
    if backtrack_addr is None:
        ERROR_PRINT(f"Neighbor {backtrack_station_name} is now INACTIVE. Unable to backtrack. Discarding packet...")
        
    # If a packet can back-track, decrease the indexing position and send it to the next neighbor in the list
    else:
        msg_toSend['visited_index'] -= 1
        UDP_sendMessage(targetAddress=backtrack_addr, targetStationName=backtrack_station_name, packetType=route_outcome, msg=msg_toSend)



# --------------------- UDP/TCP Handling ---------------------
def handle_client_connection(TCP_clientConnection):
    try:
        request = TCP_clientConnection.recv(4096).decode()
        headers, body = request.split('\r\n\r\n', 1)
        first_line = headers.splitlines()[0]
        method, path, _ = first_line.split()

        if method == 'GET' and 'to=' in path:
            query = urllib.parse.urlparse(path).query
            params = urllib.parse.parse_qs(query)
            selected_destination = params.get('to', [''])[0]
            selected_time = params.get('leave', [''])[0] # ASSUME TIME IS SENT IN THE URL -> USING THE WEBPAGE GIVEN WITH THE PROJECT CODE
            if selected_destination in STATION.get_destinations():
                departure = find_direct_or_query_indirect(selected_time, selected_destination, TCP_clientConnection)
                if departure:
                    response_message = f"Depart at {departure['departure_time']} from {departure['departure_station']}, arrive by {departure['arrival_time']} at {departure['arrival_station']}."
                    TCP_sendMessage(msg=response_message, socket_TCP=TCP_clientConnection)
                    return
                else:
                    response_message = f"There is no journey from {STATION_NAME} to {selected_destination} leaving after {selected_time}"
                    TCP_sendMessage(msg=response_message, socket_TCP=TCP_clientConnection)
            elif (STATION.is_time_after_last_departure(selected_time)):
                response_message = f"There is no journey from {STATION_NAME} to {selected_destination} leaving after {selected_time}"
                TCP_sendMessage(msg=response_message, socket_TCP=TCP_clientConnection)
            
            find_direct_or_query_indirect(selected_time, selected_destination, TCP_clientConnection)

    except Exception as e:
        ERROR_PRINT(f"Error handling client connection. ERROR: {e}")



def handle_udp_message(data, addr):
    global STATION, UDP_SOCKET, sent_messages, active_routingRequests # Get access to methods
    packet = json.loads(data.decode())
    packetType = packet['type']
    packetID = packet["packetID"]

    #VERBOSE_PRINT_EMPTY()
    DEBUG_PRINT(f"Full UDP Packet Received: {packet}")

    if packetType == ROUTE_QUERY:
        message = packet['msg']
        UDP_sendMessage(targetAddress=addr, targetStationName=packet['originStationName'], packetType=ACK, ACK_responseID=packetID)
        if STATION_NAME in message['visited']:
            DEBUG_PRINT(f"Query received already processed here, dropping to prevent loop. ({message['destination']}-->{message['visited'][0]})")
            return
        destination = message['destination']
        time = message['time']
        departure = STATION.find_next_departure(time, destination)

        if departure:
            visited = message.get('visited', None)
            if visited is None:
                ERROR_PRINT(f"Could not get visited list in packet: {packet}")
                return
            
            current_station = {
                "station_name": STATION_NAME,
                "arrival_time": time,
                "departure_time": departure['departure_time'],  # to be updated once the next departure is known
                "service_name": departure['service_name']     # to be updated once the next departure is known
            }
            visited.append(current_station) # Add final station to route
            response = {
                "type": "success",
                "departure_time": departure['departure_time'],
                "arrival_time": departure['arrival_time'],
                "destination": destination,
                "original_time": message["original_departure_time"],
                "leaving_service": message["original_service"],
                "route": visited,
                "request_id": message["request_id"],
                "visited_index": (len(visited) - 2) # Remove 1 to adjust for indexing at 0, then remove another to go to previous station
            }
            VERBOSE_PRINT(f"Route request fulfilled: {message['visited'][0]} ({departure['departure_time']}) --> {message['destination']} ({departure['arrival_time']})")
            
            # Find the back-tracking route (grab the last element in visited list)
            backtrack_station_name = response['route'][response['visited_index']]['station_name']
            backTrack(route_outcome=ROUTE_SUCCESS, backtrack_station_name=backtrack_station_name, msg_toSend=response)
            
        # Requires another batch of requests to neighbors
        else:
            # Forward the query to other neighbors if not found
            query_neighbors(message)

    elif (packetType == ROUTE_FAIL) or (packetType == ROUTE_SUCCESS):
        DEBUG_PRINT(f"Received {packetType} route: {packet['msg']}")
        UDP_sendMessage(targetAddress=addr, targetStationName=packet['originStationName'], packetType=ACK, ACK_responseID=packetID)

        # Packet is at intermediary server (not at origin yet)
        if packet['msg']['visited_index'] > -1:
            backtrack_station_name = packet['msg']['route'][packet['msg']['visited_index']]['station_name']
            backTrack(route_outcome=packetType, backtrack_station_name=backtrack_station_name, msg_toSend=packet['msg'])

        # Packet has reached origin server (starting point)
        elif active_routingRequests is not None:
            request_id = packet['msg']['request_id']
            if request_id in active_routingRequests:
                active_routingRequests[request_id]['responses'].append(packet['msg'])

    elif packetType == PING:
        UDP_sendMessage(targetAddress=addr, targetStationName=packet['originStationName'], packetType=ACK, ACK_responseID=packetID)
        
    elif packetType == ACK:
        if packetID in sent_messages:
            # Get the original packet sent under that ID
            originalPacket = sent_messages[packetID]['packet']

            # If the message was a PING (to find a neighbor)
            if originalPacket['type'] == PING:
                station_name = packet['originStationName']
                active_neighbors[station_name] = addr # {'stationname': (ip,port)}
                VERBOSE_PRINT(f"UDP Neighbour Discovered: {station_name} ({addr[0]}:{addr[1]})")
            
            # Delete acknowledged packet
            del sent_messages[packetID]
            DEBUG_PRINT(f"ACK received for packet ID: {packetID} (Type: {originalPacket['type']})")



# --------------------- Route Checking ---------------------
def check_routing():
    global active_routingRequests
    
    if (active_routingRequests is None) or (active_routingRequests == {}):
        return
    
    current_time = datetime.now()
    for request_id, request_info in list(active_routingRequests.items()):
        if ((current_time - request_info['time_sent']).total_seconds() > ROUTE_RESPONSE_TIME):
            successful_routes = [resp for resp in request_info['responses'] if resp['type'] == "success"]

            if successful_routes:
                # Sort routes by arrival time
                successful_routes.sort(key=lambda x: datetime.strptime(x['arrival_time'], '%H:%M'))
                best_route = successful_routes[0]
                VERBOSE_PRINT(f"Route successfully found for: {STATION_NAME} ({best_route['departure_time']}) ==> {best_route['destination']} ({best_route['arrival_time']})")
                route_info = get_routeInfo(route=best_route['route'], final_stationName=best_route['destination'], final_time=best_route['arrival_time'])
                msg = f"Depart at {best_route['original_time']} from {STATION_NAME} using service {best_route['leaving_service']}, arrive by {best_route['arrival_time']} at {best_route['destination']}. Use the route:<br>" + route_info

            elif (len(request_info['responses']) > 0):
                # No successful route found, check for failure message
                failure_route = request_info['responses'][0]  # Assuming fail message if no success
                msg = f"There is no journey from {STATION_NAME} to {failure_route['destination']} leaving after {request_info['time_sent']}"
            else:
                msg = f"A server error occured. There may not be any possible route to the destination."
            TCP_sendMessage(msg=msg, socket_TCP=request_info["socket"])
            del active_routingRequests[request_id]



def get_routeInfo(route, final_stationName, final_time):
    route_info = ""
    routeLength = len(route)
    for serviceNum in range(routeLength):
        # Set the final details (as its not in the route list)
        if serviceNum == routeLength-1:
            arrival_stationName = final_stationName
            arrival_time = final_time
        # If not final entry, get them from the route list
        else:
            # Access service one node ahead to get arrival information
            arrival_stationName = route[serviceNum+1]['station_name']
            arrival_time = route[serviceNum+1]['arrival_time']

        route_info += f"Catch {route[serviceNum]['service_name']} from {route[serviceNum]['station_name']}, at time {route[serviceNum]['departure_time']}, to arrive at {arrival_stationName} at time {arrival_time}. "
    return route_info



# --------------------- Packet Sending ---------------------
def TCP_sendMessage(msg, socket_TCP):
    if socket_TCP is not None:
        try:
            # Attempt to check if the connection is still open by receiving data without blocking.
            socket_TCP.setblocking(0)
            try:
                data = socket_TCP.recv(16)
                if len(data) == 0:
                    raise ConnectionResetError("Socket connection closed by the client.")
            except BlockingIOError:
                # No data received, which means the connection is still open.
                pass

            response_html = f"""HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><h1>Travel Plan</h1><p>{msg}</p><a href="#" onclick="window.history.back(); return false;">Return</a></body></html>"""
            socket_TCP.sendall(response_html.encode())
            socket_TCP.close()
            DEBUG_PRINT(f"Sent TCP packet with message: {msg}")
        except Exception as e:
            ERROR_PRINT(f"Error while sending message: {msg}. ERROR: {str(e)}")
            socket_TCP.close()
            

    else:
        ERROR_PRINT(f"Could not send TCP message as no connection exists. Message:{msg}")



# Handle sending UDP message
# When sending packets, targetAddress and packetType MUST be filled
# packetType: ACK, PING, ROUTE_SUCCESS, ROUTE_PING
# When sending an ACK, ACK_responseID must be set to the messageID that its acknowledging (IF NOT SET IT DOESNT ACKNOWLEDGE CORRECTLY)
# When resending the same packet (packet retry), set resendPacket to the packet to resend and set the same packetType.
def UDP_sendMessage(targetAddress, packetType, msg='None', ACK_responseID=None, resendPacket=None, targetStationName=None):
    global sent_messages, UDP_SOCKET
    targetAddress = targetAddress

    # Check correct target adress
    if (targetAddress is None) or (len(targetAddress) != 2):
        ERROR_PRINT(f"Target address for UDP message is not valid {targetAddress}. Reqires: (IP, Port)")
        return
    elif packetType is None:
        ERROR_PRINT(f"packetType must be set when sending a packet. Types: ACK, PING, ROUTE_PING, ROUTE_SUCCESS")
        return
    
    # Correct port if needed (needs to be an int)
    targetAddress = (targetAddress[0], int(targetAddress[1]))

    # Get new packet ID
    if packetType != ACK:
        packetID = str(uuid.uuid4())
    else:
        packetID = ACK_responseID

    if packetID is None:
        ERROR_PRINT(f"Could not form UDP packet without ID: {STATION_NAME} --> {targetAddress[0]}:{targetAddress[1]} >> Type: {packetType}, MSG: {msg}")
        return
    
    # Form the packet to send
    if resendPacket is None:
        packet = {
            'type': packetType,
            'originStationName': STATION_NAME,
            'packetID': packetID,
            'msg': msg
        }
    
    # Copy old packet if its being resent
    else:
        packet = resendPacket
        packetID = packet['packetID'] # Get the old ID (prevents a new one from being generated)

    # Add to sent messages list (if not ACK or its a retry)
    if (packetType != ACK) or (resendPacket is not None):
        if packetID in sent_messages:
            ERROR_PRINT(f"Could not send packet as ID is NOT UNIQUE: {STATION_NAME} --> {targetAddress[0]}:{targetAddress[1]} >> Type: {packetType}, MSG: {msg}, ID: {packetID}")
            return

        sent_messages[packetID] = {
            'time_latestSent': datetime.now(),
            'attempts': 1,
            'targetAddress': targetAddress,
            'packet': packet
        }

    # Send the packet to target address
    prefix_packetRetry = " (RETRY)" if resendPacket else ""
    prefix_targetStation = targetStationName if (targetStationName is not None) else (f"{targetAddress[0]}:{targetAddress[1]}")
    DEBUG_PRINT(f"Sending UDP Packet"+prefix_packetRetry+f": {STATION_NAME} --> "+prefix_targetStation+f" >> Type: {packetType}, MSG: {msg}, ID: {packetID}")
                           
    UDP_SOCKET.sendto(json.dumps(packet).encode(), targetAddress)



# --------------------- Neighbor Handling ---------------------
def check_and_resend_messages():
    global UDP_SOCKET, sent_messages

    # If no sent messages to check, return
    if sent_messages == {}:
        return

    current_time = datetime.now() # Set current time to remain constant throughout the function
    delete = []  # Packets to delete

    # As resending messages when checking them, must take copy of current sent messages
    for packetID in list(sent_messages.keys()):
        info = sent_messages[packetID]
        if (current_time - info['time_latestSent']).total_seconds() > UDP_ROUND_TRIP_TIME:
            packet = info['packet'] # Grab the whole packet

            # If the packet has more attempts left
            if info['attempts'] < UDP_MAX_SEND_ATTEMPTS:
                sent_messages[packetID]['time_latestSent'] = current_time
                sent_messages[packetID]['attempts'] += 1
                UDP_sendMessage(targetAddress=info['targetAddress'], packetType=packet['type'], resendPacket=packet)

            else:
                delete.append((packetID, info['targetAddress']))
    
    # Delete packet and drop neighbor as active
    for (packetID, addr) in delete:
        VERBOSE_PRINT(f"Packet ({packetID}) (Type: {sent_messages[packetID]['packet']['type']})  failed to send. Dropping packet...")
        del sent_messages[packetID]
        drop_activeNeighbor(addr)



def drop_activeNeighbor(addr):
    addr_host = addr[0]
    addr_port = addr[1]
    for station_name, active_addr in active_neighbors.items(): # Dont need station name
        if (addr_port == active_addr[1]) and (addr_host == active_addr[0]):
            del active_neighbors[station_name]
            VERBOSE_PRINT(f"Dropping inactive neighbor {station_name} ({addr})")
            return

        # Handle localhost IPs
        elif (addr_port == active_addr[1]) and ((addr_host == '127.0.0.1') or (addr_host == 'localhost')):
            del active_neighbors[station_name]
            VERBOSE_PRINT(f"Dropping inactive neighbor {station_name} ({addr})")
            return


# Ping neighbors
# all: To ping ALL neighbors
# inactive: To retry ping inactive neighbors
def pingNeighbors(all=False, inactive=False):
    global active_neighbors

    if all:
        # Reset existing active neighbors
        active_neighbors = {}

        for neighbor_addr in NEIGHBORS:
            neighbor_host, neighbor_port = neighbor_addr.split(':')
            UDP_sendMessage(targetAddress=(neighbor_host, neighbor_port), packetType=PING)

    elif inactive:
        for neighbor_addr in NEIGHBORS:
            neighbor_host, neighbor_port = neighbor_addr.split(':')
            neighbor_port = int(neighbor_port) # Ensure its an int

            # Check if the neighbor is an active neighbor at all
            isActive = False
            for _, active_addr in active_neighbors.items(): # Dont need station name
                if (neighbor_port == active_addr[1]) and (neighbor_host == active_addr[0]):
                    isActive = True
                    break
                # If all stations are hosted on localhost, the IP reported by sockets may not match the adjacency table
                elif (neighbor_port == active_addr[1]) and ((neighbor_host == '127.0.0.1') or (neighbor_host == 'localhost') or (neighbor_host == '0.0.0.0')):
                    isActive = True
                    break
            
            # If the neighbor is not active, ping it
            if not isActive:
                UDP_sendMessage(targetAddress=(neighbor_host, neighbor_port), packetType=PING)



# --------------------- Main ---------------------
def main():
    # Edit global variables
    global TCP_SOCKET, UDP_SOCKET, STATION
    parse_arguments() # Set global variables from run arguments
    STATION = StationTimetable(STATION_NAME)

    setup_sockets() 
    sockets_list = [TCP_SOCKET, UDP_SOCKET]

    time.sleep(3)
    pingNeighbors(all=True) # Ping all neighbors if some are already running
    

    VERBOSE_PRINT(f"Station '{STATION_NAME}' RUNNING -- TCP:{TCP_PORT} & UDP:{UDP_PORT}")
    pingCounter = 0
    while True:
        time.sleep(0.05)
        pingCounter += 1

        # Check for unacknowledged packets
        check_and_resend_messages()

        # Check on the current routing request (if one exists)
        check_routing()

        # Check for any new neighbors every certain number of loops (allows for ping requests to come back)
        if pingCounter >= 50:
            STATION.update_timetable() # Update timetable if it was modified.
            pingNeighbors(inactive=True)
            pingCounter = 0
        
       
        read_sockets, _, exception_sockets = select.select(sockets_list, [], sockets_list, 0.1)

        for notified_socket in read_sockets:
            if notified_socket == TCP_SOCKET:
                TCP_clientConnection, client_address = TCP_SOCKET.accept()
                TCP_clientConnection.setblocking(0)
                handle_client_connection(TCP_clientConnection)
                TCP_clientConnection = None # Ensure variable is wiped
            
            elif notified_socket == UDP_SOCKET:
                data, addr = UDP_SOCKET.recvfrom(4096)
                handle_udp_message(data, addr)

        for notified_socket in exception_sockets:
            sockets_list.remove(notified_socket)
            notified_socket.close()


if __name__ == "__main__":
    main()
