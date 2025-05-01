// topic_trie.h
#ifndef TOPIC_TRIE_H
#define TOPIC_TRIE_H

#include "client_server.h"
#include "protocol.h"

typedef struct client client_t;

// linked list of subscribers on each node
typedef struct client_list {
    client_t          *cl;
    struct client_list *next;
} client_list_t;

// what kind of link we are to our parent
typedef enum {
    CHILD_NAME,   // exact‐match named child
    CHILD_PLUS,   // '+' wildcard child
    CHILD_STAR    // '*' wildcard child
} child_type_t;

// trie node
typedef struct topic_node {
    struct child {
        char              *name;
        struct topic_node *node;
        struct child      *next;
    } *children;

    struct topic_node *plus_child;
    struct topic_node *star_child;

    client_list_t     *subscribers;

    // for pruning
    struct topic_node *parent;
    child_type_t       ptype;
    char               *pname;
} topic_node_t;

// to let a client quickly unsubscribe/disconnect
typedef struct sub_ref {
    topic_node_t     *node;
    struct sub_ref   *next;
} sub_ref_t;

topic_node_t *node_create(topic_node_t *parent,
						  child_type_t ptype,
						  const char *pname);
int trie_subscribe(topic_node_t *root, client_t *cl, const char *pattern);
int trie_unsubscribe(topic_node_t *root, client_t *cl, const char *pattern);
void trie_publish(topic_node_t *root, const char *topic, const char *buf, size_t len);
void cleanup_client_subscriptions(topic_node_t *root, client_t *cl);

#endif // TOPIC_TRIE_H
