import socket
import sys      # For argument passing

def create_tcp_server(port):
    # Create a TCP/IP socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    # Set the SO_REUSEADDR flag on the socket to allow the address to be reused
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Bind the socket to the server address and port
    server_address = ('localhost', port)
    server_socket.bind(server_address)

    # Listen for incoming connections
    server_socket.listen(1)
    print(f"Server is running on port {port}...")

    while True:
        # Wait for a connection
        print('waiting for a connection')
        connection, client_address = server_socket.accept()

        try:
            print(f'connection from {client_address}')

            # Receive the data in small chunks
            request = connection.recv(1024).decode()
            print(f"Received request: {request}")

            # Parse the HTTP request
            lines = request.splitlines()
            if lines:
                request_line = lines[0]  # e.g., "GET / HTTP/1.1"
                print(f"Request Line: {request_line}")

            # Prepare HTTP response
            http_response = """HTTP/1.1 200 OK
Content-Type: text/html
Connection: close

<html>
<body>
<h1>Hello from Python TCP Server!2</h1>
</body>
</html>
"""

            # Send HTTP response
            connection.sendall(http_response.encode())
        finally:
            # Clean up the connection
            connection.close()

def main():
    # Check if at least one argument is passed (excluding the script name)
    if len(sys.argv) > 1:
        print(f"Arguments received: {sys.argv[1:]}")
    else:
        print("No arguments received.")

if __name__ == "__main__":
    main()