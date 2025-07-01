from http.server import BaseHTTPRequestHandler, HTTPServer

class PCMHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers['Content-Length']) if self.headers.get('Content-Length') else 0
        with open("received.pcm", "ab") as f:
            while True:
                chunk = self.rfile.read(4096)
                if not chunk:
                    break
                f.write(chunk)
        self.send_response(200)
        self.end_headers()

server_address = ('', 8000)
httpd = HTTPServer(server_address, PCMHandler)
print("Listening on port 8000...")
httpd.serve_forever()
