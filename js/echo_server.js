const http = require('http');

const server = http.createServer((req, res) => {
  if (req.method === 'POST' && req.url === '/echo') {
    req.pipe(process.stdout);
    res.end("received\n");

  } else {
    res.statusCode = 404;
    res.end();
  }
});

server.listen(process.argv[3] || 3000, process.argv[2] || 'localhost');
