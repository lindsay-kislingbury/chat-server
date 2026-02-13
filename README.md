# Chat Server

A multi-client TCP chat server and client written in C for a Linux environment.

## Features
- Supports up to 10 concurrent clients
- Threaded connection handling with pthreads
- Mutex-synchronized message broadcasting
- Timestamped chat history saved to file
- Graceful shutdown via signal handling (SIGINT/SIGTERM)
- Username registration on connect

## Building

Requires GCC and pthreads (any Linux environment).
```
make
```

## Running
```
./chat_server <port>
./chat_client <port>
```

## Background

Built for CS 2600 (Systems Programming) at Cal Poly Pomona.
