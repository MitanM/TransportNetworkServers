import asyncio
import sys            # For argument passing
from socket import AF_INET, SOCK_DGRAM, SOCK_STREAM, socket


# Station class for neighboring stations
class Station:
    def __init__(self, ip, port):
        self.ip = ip
        self.port = port



# Global variables
station_name = ''
port_TCP = -1
port_UDP = -1
neighbourStations = []



async def handle_client_connection(reader, writer, neighborStation):
    data = await reader.read(1024)
    message = data.decode('utf-8')
    addr = writer.get_extra_info('peername')
    print(f"Received {message} from {addr}")

    # Send HTML form to the client
    response_content = """HTTP/1.1 200 OK
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
    writer.write(response_content.encode('utf-8'))
    await writer.drain()
    writer.close()



async def start_tcp_server(host, port, neighborStation):
    server = await asyncio.start_server(
        lambda r, w: handle_client_connection(r, w, neighborStation),
        host, port)
    addr = server.sockets[0].getsockname()
    print(f'Serving on {addr}')

    async with server:
        await server.serve_forever()



async def udp_server(host, port):
    loop = asyncio.get_running_loop()
    transport, protocol = await loop.create_datagram_endpoint(
        lambda: UDPProtocol(),
        local_addr=(host, port))
    print(f'Listening for UDP messages on {port}')



class UDPProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        message = data.decode()
        print(f"Received {message} from {addr}")



def check_errors(station_name, port_TCP, port_UDP, neighbourStations):
    if station_name == '' or port_TCP == -1 or port_UDP == -1 or len(neighbourStations) == 0:
        return True
    return False



def initialise(port_TCP, port_UDP, neighbourStations):
    asyncio.run(start_tcp_server('localhost', port_TCP, neighbourStations[0]))
    asyncio.run(udp_server('localhost', port_UDP))



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
        initialise(port_TCP, port_UDP, neighbourStations)

    else:
        print("Not enough arguments received! Need minimum 4")
        print("Arguments: StationName TCP-PORT UDP-PORT IP:PORT (IP2:PORT2...)")







if __name__ == "__main__":
    main()
