Our final goal is to add WebTransport support into emscripten.

There are a few important files you ALWAYS have to read at the beginning of each session:
system/include/emscripten/webtransport.h
src/lib/libwebtransport.js
test/webtransport/test_webtransport.c

Those 3 files above are the files we mainly work on. 

Additionally we are learning how to implement this by reading the following files each session:
src/lib/libwebsocket.js
system/include/emscripten/websocket.h

While we learn from those files we do not edit them! You learn how emscripten works internally from those files but that doesnt mean you copy the API!!!
We want to be as close as possible to the actually webtransport api, in the webtransportdocs directory I downloaded the full WebTransport API for you!
Make sure to read every single file inside the webtransportdocs directory before you start coding! One difference to the WebSocket API is that WebTransport doesnt have on message callbacks but you call the read method on it similiar to how you call recv in a loop, thats why we need to make a modern API for it without copying the old API.

Also make a web request to https://developer.mozilla.org/en-US/docs/Web/API which contains a list of all the API docs for the web and read any other WebTransport API docs that you need. 
Make the request before you start coding, but you can read the docs later if you need a better understanding of any other docs that I did not fully download. You can anytime during your coding do such a web request!

Some properties are experimental and not supported by chrome, you do not have to implement those. 
Here is a list of some of them, but this is not a complete list, you can find more about it in the https://developer.mozilla.org/en-US/docs/Web/API docs if you want to check for it.
We only care about chrome, if chrome supports it then we support it. If chrome doesnt support it then we dont support it. Other browser dont matter.

Use the following command to clear the compilers cache after changing any files and before compiling:
./emcc --clear-cache

After making changes YOU MUST build the test_webtransport.c with this command:
./emcc test/webtransport/test_webtransport.c -lwebtransport.js -o test/webtransport/index.html --emrun

After building the test YOU MUST run the test with the following command:
./emrun.bat test/webtransport/index.html --browser="C:\Program Files\Google\Chrome\Application\chrome.exe" --kill_exit --browser_args="--headless --disable-gpu --disable-software-rasterizer"

At the following URL you find a WebTransport echo server:
https://wt-ord.akaleapi.net:6161/echo

You MUST make a web request to read the client.js for the above echo server before starting work:
https://wt-ord.akaleapi.net/echo/client.js
You DO NOT copy this API. This is only as reference if you have issues with the echo server
I confirmed that the test client works with the public echo server. That should help you to figure out where a bug could be.

You have to connect to it in your test code and all tests MUST use the echo server.
Every feature has to be tested through a real connection.

Try to work independantly without reporting back often and run as long as possible. 
Your working directory is also a git repo and you are on a branch specially made for you. 
DO NOT CHANGE BRANCHES AND DO NOT PUSH COMMITS. 
We are working fully local, you SHOULD commit but you DO NOT push those commits.

We are working agile and in small steps. Each commit MUST compile the test code and MUST pass the tests.
Focus on making small API changes, getting those to work and then test them and only then tackle the next ones.