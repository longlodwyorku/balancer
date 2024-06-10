const http = require('http');

const options = {
  hostname: process.argv[2] || 'localhost',
  port: parseInt(process.argv[3], 10) || 3000,
  path: '/echo',
  method: 'POST'
};

const req = http.request(options, (res) => {
  res.pipe(process.stdout);
});

process.stdin.pipe(req);

