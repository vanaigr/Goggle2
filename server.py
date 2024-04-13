from http.server import BaseHTTPRequestHandler, HTTPServer
import time
import requests

hostName = "localhost"
serverPort = 1619

url = 'https://google.com/search?q='

def removeNoscript(str):
    before = str.find('/head') + 6
    start = str.find('noscri', before) - 1
    end = str.find('noscri', start + 10) + 9
    str = '<!DOCTYPE HTML><html lang="en"><head></head>' + str[before:start] + str[end:]

    return str.replace('google.com', 'example.com')

class MyServer(BaseHTTPRequestHandler):
    def do_GET(self):
        query = self.path[10:]
        print(query)
        start = time.time()
        response = requests.get(url + query).text
        end = time.time()
        print("Elapsed: " + str(end - start) + 's')
        response = removeNoscript(response)
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        #self.wfile.write(bytes('<!DOCTYPE html><head><body>Hello, world!</body></html>', 'utf-8'))
        self.wfile.write(bytes(response, "utf-8"))

if __name__ == "__main__":
    webServer = HTTPServer((hostName, serverPort), MyServer)
    print("Server started http://%s:%s" % (hostName, serverPort))

    try:
        webServer.serve_forever()
    except KeyboardInterrupt:
        pass

    webServer.server_close()
    print("Server stopped.")
