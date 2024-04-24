import socket
import sys          # For argument passing
import threading



# Station class for neighboring stations
class Station:
    def __init__(self, ip, port):
        self.ip = ip
        self.port = port



# Global variables
station_name = ''
port_TCP = -1       # Web browser communication
port_UDP = -1       # Other stations communication with this station
neighbourStations = []



def handle_client_connection(client_socket, neighborStation):
    try:
        # Receive the HTTP request
        request = client_socket.recv(1024).decode('utf-8')
        print(f"Received request: {request}")

        # Extract the path or data from the HTTP GET request
        headers = request.split('\n')
        first_line = headers[0].split()
        method = first_line[0]
        if method == "GET":
            path = first_line[1]
            if path == '/':
                # Send HTML form to the client
                response_content = """\
HTTP/1.1 200 OK
Content-Type: text/html

<html>
<head><title>Station Message Sender</title></head>
<body>
    <h1>Send Message to Other Station</h1>
    <form method="post">
        <input type="text" name="message" placeholder="Enter message to send" required>
        <button type="submit">Send</button>
    </form>
</body>
</html>
"""
                client_socket.sendall(response_content.encode('utf-8'))
            elif path.startswith('/?message='):
                # Extract message and send it via UDP
                message = path.split('=')[1]
                send_udp_message(message, neighborStation)
                response_content = f"HTTP/1.1 200 OK\nContent-Type: text/html\n\n<p>Message sent: {message}</p>"
                client_socket.sendall(response_content.encode('utf-8'))

        elif method == "POST":
            # Handling POST to send message
            content_length = int([x for x in headers if x.startswith('Content-Length:')][0].split()[1])
            message = request.split('\r\n\r\n')[1][8:]  # skip 'message='
            send_udp_message(message, neighborStation)
            response_content = f"HTTP/1.1 303 See Other\nLocation: /?message={message}\n\n"
            client_socket.sendall(response_content.encode('utf-8'))

    finally:
        client_socket.close()



def start_tcp_server(host, port, neighborStation):
    try:
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind((host, port))
        server_socket.listen(5)
        print(f"TCP Server listening on {host}:{port}")
        while True:
            client_socket, _ = server_socket.accept()
            client_thread = threading.Thread(target=handle_client_connection, args=(client_socket, neighborStation))
            client_thread.start()
    except socket.error as e:
        print(f"Failed to start the TCP server: {e}")


def send_udp_message(message, neighborStation):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(message.encode(), (neighborStation.ip, neighborStation.port))



def udp_listener(host, port):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((host, port))
        print(f"Listening for UDP messages on {port}")
        while True:
            data, addr = sock.recvfrom(1024)
            print(f"Received from {addr}: {data.decode()}")



def check_errors(station_name, port_TCP, port_UDP, neighbourStations):
    if station_name == '' or port_TCP == -1 or port_UDP == -1 or len(neighbourStations) == 0:
        return True
    return False



def initialise():
    global port_UDP, port_TCP

    # Start UDP listener in a separate thread
    threading.Thread(target=udp_listener, args=('localhost', port_UDP)).start()

    # Start TCP server to handle client connections  -- FOR FIRST NEIGHBOUR
    start_tcp_server('localhost', port_TCP, neighbourStations[0])



def main():
    global station_name, port_TCP, port_UDP, neighbourStations
    # Check if at least one argument is passed (excluding the script name)
    if len(sys.argv) >= 4:
        print(f"Arguments received: {sys.argv[1:]}")
        station_name = sys.argv[1]
        port_TCP = int(sys.argv[2])
        port_UDP = int(sys.argv[3])
        
        # Add neighbour stations
        for i in range(4, len(sys.argv)):
            ip_port = sys.argv[i].split(':')
            if len(ip_port) == 2:
                ip = ip_port[0]
                port = int(ip_port[1])
                stn = Station(ip, port)
                neighbourStations.append(stn)
    
        if check_errors(station_name, port_TCP, port_UDP, neighbourStations):
            print("Failed error check.")
            print(station_name, port_TCP, port_UDP)
            return
        
        # Run the setup
        initialise()

    else:
        print("Not enough arguments received! Need minimum 4")
        print("Arguments: StationName TCP-PORT UDP-PORT IP:PORT (IP2:PORT2...)")

if __name__ == "__main__":
    main()