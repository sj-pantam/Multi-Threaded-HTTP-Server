# Multi-Threaded HTTP Server

This project is a multi-threaded HTTP server designed to efficiently handle multiple client requests concurrently. The server supports basic HTTP methods like `GET` and `PUT` and uses synchronization mechanisms such as mutexes and read-write locks to ensure safe concurrent access to resources.

## Overview

### Multithreading

Multithreading allows the server to handle multiple client connections simultaneously. By creating a pool of worker threads, the server can process several requests at the same time, improving its responsiveness and scalability.

### Synchronization Mechanisms

#### Mutex (`pthread_mutex_t`)

A mutex (mutual exclusion) lock is used to protect shared data structures from concurrent access. Only one thread can hold the mutex at a time, ensuring that critical sections of the code are executed by one thread at a time.

- **Locking**: `pthread_mutex_lock(&mutex);`
- **Unlocking**: `pthread_mutex_unlock(&mutex);`

#### Read-Write Lock (`rwlock_t`)

A read-write lock allows multiple threads to read a resource concurrently (read lock), but only one thread to write to the resource (write lock), ensuring safe concurrent access.

- **Read Lock**:
  - **Acquire**: `reader_lock(val);`
  - **Release**: `reader_unlock(val);`
- **Write Lock**:
  - **Acquire**: `writer_lock(lock);`
  - **Release**: `writer_unlock(lock);`

## Code Structure

### GET Request Handling

The server handles `GET` requests by acquiring a read lock on the requested resource, allowing multiple threads to read the resource concurrently. If the resource is a file, it reads and returns the file's contents to the client.

### PUT Request Handling

The server handles `PUT` requests by acquiring a write lock on the requested resource, ensuring exclusive access while writing. It writes the data received from the client to the specified resource.

## Running the Server

### Prerequisites

- GCC (GNU Compiler Collection)
- Make

### Building the Server

To compile the server:

```sh
make
```

### Running the Server

To run the server, use the following command:

```
./httpserver [-t threads] <port>
```
-t threads (optional): Number of worker threads to use (default is 4).
<port>: The port number on which the server will listen for incoming connections.
