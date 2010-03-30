#### COMPILE

You need to fetch the http-parser submodule, by typing:
<pre>
git submodule init
git submodule update
</pre>


#### Run demo
1 - run `make clean all`  
2 - run `./cometd`

3 - In a terminal, run:
<pre>
curl "http://127.0.0.1:1234/meta/authenticate?uid=1729&sid=my-secret-sid&payload={}"
curl "http://127.0.0.1:1234/meta/newchannel?name=public-channel&key=c4rp2n3H5KzX"
curl "http://127.0.0.1:1234/meta/connect?name=public-channel&uid=1729&sid=my-secret-sid"
</pre>
The last HTTP call (`/meta/connect`) is blocking.


4 - And then, in another terminal:
<pre>
curl "http://127.0.0.1:1234/meta/publish?name=public-channel&data=hello-world-of-comet&uid=1729&sid=my-secret-sid"
</pre>

5 - The `/meta/connect` call returns, with the data published in the channel.

→ That's it for the proof of concept!

#### TODO
* Complete iframe page.
** Add connection timeout → reconnect after N seconds
** Add timeout on reconnect
* Add client capability detection (possible streaming or close connection after every push).
* Add custom payload for users (ex: {uid: 1729, login: "nff"})
* Add JSONP callback parameter.
* Add support for HTML5 WebSockets
* Remove user authentication?
* Remove user subscriptions?
* Lots of cleanup code.
* Add timeouts on the server side
* Add enums for returns messages, especially in http_dispatch.{c,h}
* Prepare a better demo.
