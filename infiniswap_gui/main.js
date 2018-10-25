
var ipc = require('node-ipc');

var url = require('url');

var socketId = 'icp-test';
ipc.config.id = 'hello';
ipc.config.socketRoot = '/tmp/';
ipc.config.appspace = '';

ipc.config.retry = 1500;


var https = require('http').createServer(handler); //require http server, and create server with function handler()
var fs = require('fs'); //require filesystem module
var io = require('socket.io')(https); //require socket.io module and pass the http object (server)

https.listen(4000); //listen to port 8080

var pre_path = "";
function handler(req, res) { //create server
    path = url.parse(req.url).pathname;
    // if (path != pre_path) {
        console.log("path is: ", path);
        ipc.of[socketId].emit(
            'message',  //any event or message type your server listens for
            path,
        )
    //}
    pre_path = path;
    fs.readFile(__dirname + '/public/slab.html', function (err, data) { //read file index.html in public folder
        if (err) {
            res.writeHead(404, { 'Content-Type': 'text/html' }); //display 404 on error
            return res.end("404 Not Found");
        }
        res.writeHead(200, { 'Content-Type': 'text/html' }); //write HTML
        res.write(data); //write data from index.html
        return res.end();
    });
}

io.sockets.on('connection', function (socket) {// WebSocket Connection
    var lightvalue = 0; //static variable for current status
    socket.on('light', function (data) { //get light switch status from client
        lightvalue = data;
        console.log(lightvalue); //turn LED on or off, for now we will just show it in console.log
        ipc.of[socketId].emit(
            'message',  //any event or message type your server listens for
            'hello' + data,
        )
    });
});


ipc.connectTo(
    socketId,
    function () {
        ipc.of[socketId].on(
            'connect',
            function () {
                console.log("Connected!!");
                ipc.log('## connected to world ##'.rainbow, ipc.config.delay);
                ipc.of[socketId].emit(
                    'message',  //any event or message type your server listens for
                    'connection success'
                )
            }
        );
        ipc.of[socketId].on(
            'disconnect',
            function () {
                console.log("Disconnected!!");
                ipc.log('disconnected from world'.notice);
            }
        );
        ipc.of[socketId].on(
            'message',  //any event or message type your server listens for
            function (data) {
                console.log("Got a message!!");
                ipc.log('got a message from world : '.debug, data);
            }
        );
    }
);