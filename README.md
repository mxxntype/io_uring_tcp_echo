## `io_uring` echo server in C

Well, nothing fancy here really. To start the server:

```bash
just run <port>
```

To test it:

```bash
echo "Hello, server!" | nc localhost <port>
```

You should get your string back.
