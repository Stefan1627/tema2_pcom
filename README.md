# 324CC Stefan CALMAC

# Protocol Utilities

This module implements low-level send functions for the broker’s TCP protocol framing.

## File: protocol.c

### Functions

#### `int send_all(int fd, const void *buf, size_t len)`
Sends exactly `len` bytes from `buf` to socket `fd`, retrying as needed until all data is written or an error occurs.  
- **Parameters**  
  - `fd` — the destination socket file descriptor  
  - `buf` — pointer to the data to send  
  - `len` — number of bytes to send  
- **Returns**  
  - `0` on success (all bytes sent)  
  - `-1` on error (send failure)

#### `int send_message(int fd, uint16_t type, const void *payload, uint32_t len)`
Sends a framed message consisting of a 6-byte header followed by an optional payload:  
1. Constructs a `MsgHeader` with `type` and `len` converted to network byte order.  
2. Calls `send_all()` to send the header.  
3. If `len > 0`, calls `send_all()` to send the payload.  
- **Parameters**  
  - `fd` — the destination socket file descriptor  
  - `type` — message type (e.g. `MSG_SUBSCRIBE`, `MSG_PUBLISH`)  
  - `payload` — pointer to the payload data  
  - `len` — length of the payload in bytes  
- **Returns**  
  - `0` on success  
  - `-1` on error (header or payload send failure)

---

## Data Structures

- **`MsgHeader`** (defined in `protocol.h`)  
  ```c
  typedef struct {
      uint16_t type;   // message type, network byte order
      uint32_t length; // payload length, network byte order
  } MsgHeader;


# Server

This module implements the main server loop for a hybrid UDP/TCP publish–subscribe broker. It handles incoming UDP “publish” messages, TCP client connections/subscriptions, and dispatches published messages via the topic trie.

## File: server.c

### Functions

#### `ssize_t build_packet(struct sockaddr_in *src, char *buf, ssize_t payload_len)`
Prepends a header containing the publisher’s IP address and port to the UDP payload already stored in `buf`. Returns the total length (header + payload), or `payload_len` on header‐formatting error.

#### `char *extract_topic(char *msg)`
Allocates and returns a null‐terminated string containing up to `MAX_TOPIC_LEN` characters from the start of `msg`. Used to parse the topic portion before the message body.

#### `void handle_new_tcp_connection(int tcp_fd, client_t **clients, client_t **inactive_clients, int *client_count)`
Accepts a pending TCP connection on `tcp_fd`, reads the client’s ID, and:
- If the ID is already active, closes the new socket.
- If it matches an inactive client, reactivates that client (preserving subscriptions).
- Otherwise, creates a brand-new `client_t` and adds it to the active list.
Updates `*clients`, `*inactive_clients`, and `*client_count` accordingly.

#### `void run_server(int port)`
Sets up the UDP and TCP sockets bound to `port`, creates the root of the topic trie, and enters the main event loop:
1. Uses `poll()` to wait for:
   - `stdin` (“exit” command),
   - UDP messages,
   - New TCP connections,
   - Data from each connected TCP client.
2. On UDP receive:
   - Extracts topic,
   - Builds packet header,
   - Publishes via `trie_publish`.
3. On TCP accept:
   - Calls `handle_new_tcp_connection`.
4. On TCP client data or disconnect:
   - Calls `client_handle_data`; on error, moves client to inactive list.
5. On “exit” from `stdin`, breaks loop.
Cleans up all clients and sockets before returning.

#### `int main(int argc, char **argv)`
Entry point:
- Disables `stdout` buffering.
- Verifies command-line arguments (`<port>`).
- Calls `run_server(atoi(argv[1]))`.
- Returns `0` on normal exit, `1` on usage error.


# Topic Trie

This module implements a topic‐based trie data structure for a publish/subscribe messaging system. Clients can subscribe to topic patterns (with `+` and `*` wildcards), publish messages to topics, and automatically manage subscription lifecycles.

## File: topic_trie.c

### Functions

#### `int node_is_empty(topic_node_t *n)`
Checks whether a node has no subscribers and no child nodes (including named children, `+`, or `*` wildcards).

#### `void node_remove_if_empty(topic_node_t *root, topic_node_t *n)`
Recursively unlinks and frees a node if it is empty, then attempts the same on its parent up to the root.

#### `topic_node_t *node_create(topic_node_t *parent, child_type_t ptype, const char *pname)`
Allocates and initializes a new trie node of the given type (`CHILD_NAME`, `CHILD_PLUS`, `CHILD_STAR`), links it to its parent, and stores its name if applicable.

#### `topic_node_t *get_or_create_child(topic_node_t *parent, const char *name)`
Finds a named child under `parent`; if none exists, creates a new one with the given name and links it into the child list.

#### `int node_add_subscriber(topic_node_t *n, client_t *cl)`
Adds a client to the node’s subscriber list and creates a back‐reference in the client’s subscription list. Returns 0 on success, –1 on error.

#### `int trie_subscribe(topic_node_t *root, client_t *cl, const char *pattern)`
Subscribes a client to a topic pattern (e.g. `"a/+/b/*"`). Splits the pattern on `/`, walks or creates nodes for each segment (handling `+` and `*` wildcards), and finally adds the subscriber to the terminal node.

#### `int remove_subscriber_from_node(topic_node_t *root, topic_node_t *n, client_t *cl)`
Removes a client entry from a node’s subscriber list; if the node becomes empty, prunes it (and its ancestors) via `node_remove_if_empty`. Returns 0 on success, –1 if the client was not found.

#### `int trie_unsubscribe(topic_node_t *root, client_t *cl, const char *pattern)`
Unsubscribes a client from exactly one pattern. Navigates the trie without creating nodes, removes the subscriber from the target node, prunes empty nodes, and removes the back‐reference from the client.

#### `void collect(topic_node_t *n, char **T, int N, int idx, client_list_t **out)`
Recursively collects all subscribers matching the topic segments array `T[0..N-1]` from node `n`, honoring `+` wildcards (one segment) and `*` wildcards (zero or more segments). Appends matching clients to `*out`.

#### `void trie_publish(topic_node_t *root, const char *topic, const char *buf, size_t len)`
Publishes a message `buf` of length `len` to all clients subscribed to `topic`. Splits `topic` on `/`, uses `collect` to gather matches, deduplicates client entries, and invokes `send_message` for each unique client.

#### `void cleanup_client_subscriptions(topic_node_t *root, client_t *cl)`
On client disconnect or destruction, removes all of that client’s subscriptions from the trie and frees associated back‐references.

---

## Data Structures

- **`topic_node_t`**  
  Represents a node in the trie. Contains child pointers (`children`, `plus_child`, `star_child`), a subscriber list, and links to its parent.

- **`struct child`**  
  Linked‐list node for named children under a `topic_node_t`.

- **`client_list_t`**  
  Linked‐list node for subscribers attached to a `topic_node_t`.

- **`sub_ref_t`**  
  Back‐reference from a `client_t` to a `topic_node_t`, enabling efficient cleanup.

---

## Usage

1. Create a root node with `node_create(NULL, CHILD_NAME, NULL)`.  
2. Use `trie_subscribe` and `trie_unsubscribe` to manage subscriptions.  
3. Call `trie_publish` to dispatch messages.  
4. Upon client teardown, invoke `cleanup_client_subscriptions` to remove all subscriptions.

```c
// Example
topic_node_t *root = node_create(NULL, CHILD_NAME, NULL);
trie_subscribe(root, client, "sensors/+/temperature");
trie_publish(root, "sensors/kitchen/temperature", payload, payload_len);
cleanup_client_subscriptions(root, client);

# Client–Server Utilities

This module provides functions to manage TCP‐connected clients in the publish/subscribe broker: creating client structures, cleaning them up, and processing incoming subscribe/unsubscribe requests.

## File: client_server.c

### Functions

#### `client_t *client_create(int fd, const char *id)`
Allocates and initializes a new `client_t` for a connected TCP socket:
- Stores the file descriptor `fd` and copies the client identifier `id`.
- Initializes the read buffer length to zero.
- Disables Nagle’s algorithm on the socket (sets `TCP_NODELAY`).
- Returns a pointer to the new client, or `NULL` on error.

#### `void client_destroy(topic_node_t *root, client_t *c)`
Cleans up and frees a client object:
1. Calls `cleanup_client_subscriptions(root, c)` to remove all of the client’s subscriptions from the topic trie.
2. Closes the client’s socket (`c->fd`).
3. Frees the `client_t` structure itself.

#### `int client_handle_data(topic_node_t *root, client_t *c)`
Reads and processes one or more framed messages from the client’s TCP socket:
1. Reads up to `READ_BUF_SIZE` bytes into `c->read_buf`.
2. While there is at least a header’s worth of data (`uint16_t type` + `uint32_t length`):
   - Parses the message type (`MSG_SUBSCRIBE` or `MSG_UNSUBSCRIBE`) and payload length.
   - Validates the length against the buffer size.
   - If the full payload has arrived, null‐terminates it and:
     - On `MSG_SUBSCRIBE`, calls `trie_subscribe(root, c, payload)`, then sends `MSG_SUBSCRIBE_ACK`.
     - On `MSG_UNSUBSCRIBE`, calls `trie_unsubscribe(root, c, payload)`, then sends `MSG_UNSUBSCRIBE_ACK`.
   - Advances past the processed message.
3. Compacts any leftover bytes to the start of the buffer.
4. Returns `0` on success, or `-1` if the client disconnected or an error occurred (invalid length, subscription failure, etc.).

---

## Data Structures

- **`client_t`**  
  Represents a connected TCP client. Contains:
  - `int fd` — socket file descriptor  
  - `char id[16]` — client identifier  
  - `char read_buf[READ_BUF_SIZE]` — buffer for incoming data  
  - `size_t read_buf_len` — number of bytes currently in `read_buf`  
  - `client_t *next` — pointer for linked‐list of active/inactive clients


# Subscriber Client

This module implements the standalone TCP subscriber application for the publish/subscribe broker. It connects to the broker, sends subscribe/unsubscribe requests from user input, and prints incoming publications.

## File: subscriber.c

### Macros

- `SUB_LEN` / `UNSUB_LEN`  
  Lengths of the literal prefixes `"subscribe "` (10) and `"unsubscribe "` (12) used to parse user commands.
- `READ_BUF_SIZE`  
  Maximum buffer size (2048 bytes) for incoming TCP payloads.

### Functions

#### `ssize_t recv_all(int sockfd, void *buf, size_t len)`
Reads exactly `len` bytes from the TCP socket `sockfd` into `buf`, looping until all bytes arrive, the peer closes the connection (`0`), or an error occurs (`-1`).

#### `void process_payload(char *payload, size_t len)`
Interprets and prints a broker‐forwarded UDP payload whose first byte is a data‐type identifier:
- **0 (INT):** 1-byte sign + 4-byte integer (network order)  
- **1 (SHORT_REAL):** 2-byte fixed‐point number (network order)  
- **2 (FLOAT):** 1-byte sign + 4-byte mantissa + 1-byte exponent  
- **3 (STRING):** NUL-terminated text  
Prints in the format `TYPE – value` or an error if the payload is malformed.

#### `void print_packet(char *buf, size_t total_len)`
Parses and displays a published message buffer received over TCP (originally from UDP):
1. Extracts ASCII IP and port (up to spaces).  
2. Reads fixed‐width topic (`MAX_TOPIC_LEN` bytes).  
3. Prints the prefix `IP:port - topic - `.  
4. Calls `process_payload` on the remaining bytes.

#### `int handle_received_data(int sockfd)`
Reads one framed message from the broker:
1. Uses `recv_all` to read the 6‐byte header (`MsgHeader`).  
2. Validates payload length against `READ_BUF_SIZE`.  
3. Reads the payload.  
4. Dispatches based on `hdr.type`:
   - `MSG_PUBLISH`: calls `print_packet`.  
   - `MSG_SUBSCRIBE_ACK`: prints `Subscribed to topic …`.  
   - `MSG_UNSUBSCRIBE_ACK`: prints `Unsubscribed from topic …`.  
   - Other: prints raw message.  
Returns `1` on success, `0` if the server closed the connection, or `-1` on error.

#### `int main(int argc, char *argv[])`
Entry point for the subscriber application:
1. Validates arguments: `<ID_CLIENT> <IP_SERVER> <PORT_SERVER>`.  
2. Creates and connects a TCP socket to the broker.  
3. Sends the client ID followed by newline.  
4. Uses `select()` to multiplex:
   - **STDIN**: reads commands:
     - `subscribe <topic>` → sends `MSG_SUBSCRIBE`.  
     - `unsubscribe <topic>` → sends `MSG_UNSUBSCRIBE`.  
     - `exit` → exits loop.  
   - **Socket**: calls `handle_received_data` to display messages/acks.  
5. Closes the socket and exits.

## Notes

- **Estimated development time:**  
  I spent roughly **10–15 hours** over the course of a week designing and implementing this homework

- **Why a topic trie instead of a flat list/array of subscriptions?**  
  1. **Efficient lookup:**  
     With a trie, matching a published topic against all subscriptions takes time proportional to the number of topic segments (and any wildcard branches), not the total number of subscribers. In contrast, a flat list or array requires checking every subscription pattern on each publish, yielding **O(N)** pattern-matching work per message.  
  2. **Wildcard support:**  
     The trie naturally handles the `+` (single‐level) and `*` (multi‐level) wildcards by branching at the appropriate nodes, avoiding repeated string-splitting and regex-style matching for every subscriber.  
  3. **Pruning and memory locality:**  
     Unused branches are automatically removed when subscriptions are deleted, keeping the data structure compact. Children of the same parent are stored in contiguous lists, improving cache performance compared to scattered entries in an array.  
  4. **Scalability:**  
     As the number of topics and subscribers grows, the trie’s performance degrades gracefully (linear in topic depth) rather than linearly in subscriber count. This makes it well suited for high-throughput scenarios with many subscriptions and hierarchical topic namespaces.  
