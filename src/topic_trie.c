// topic_trie.c
#include "../include/topic_trie.h"

static topic_node_t *root = NULL;

// helper: is this node unused?
static int node_is_empty(topic_node_t *n) {
    return n->subscribers == NULL
        && n->children    == NULL
        && n->plus_child  == NULL
        && n->star_child  == NULL;
}

// forward
static void node_remove_if_empty(topic_node_t *n);

// create a node, linking it to parent
static topic_node_t *node_create(topic_node_t *parent,
                                 child_type_t ptype,
                                 const char *pname)
{
    topic_node_t *n = calloc(1, sizeof(*n));
    n->parent = parent;
    n->ptype  = ptype;
    if (ptype == CHILD_NAME && pname) {
        n->pname = strdup(pname);
    }
    return n;
}

// unlink & free n if empty, then recurse to parent
static void node_remove_if_empty(topic_node_t *n) {
    if (!n || n == root || !node_is_empty(n)) return;

    topic_node_t *p = n->parent;
    if (n->ptype == CHILD_NAME) {
        // unlink from parent's children list
        struct child **prev = &p->children;
        for (struct child *c = p->children; c; prev = &c->next, c = c->next) {
            if (c->node == n) {
                *prev = c->next;
                free(c->name);
                free(c);
                break;
            }
        }
    } else if (n->ptype == CHILD_PLUS) {
        p->plus_child = NULL;
    } else if (n->ptype == CHILD_STAR) {
        p->star_child = NULL;
    }
    free(n->pname);
    free(n);
    // try parent
    node_remove_if_empty(p);
}

// initialize the trie root
void trie_init(void) {
    if (!root) {
        root = node_create(NULL, CHILD_NAME, NULL);
    }
}

// find or create an exact‐match child
static topic_node_t *get_or_create_child(topic_node_t *parent,
                                         const char *name)
{
    for (struct child *c = parent->children; c; c = c->next) {
        if (strcmp(c->name, name) == 0)
            return c->node;
    }
    struct child *c = malloc(sizeof(*c));
    c->name = strdup(name);
    c->node = node_create(parent, CHILD_NAME, name);
    c->next = parent->children;
    parent->children = c;
    return c->node;
}

// add client to node->subscribers and track in client
int node_add_subscriber(topic_node_t *n, client_t *cl) {
    // allocate subscriber list entry
    client_list_t *e = malloc(sizeof(*e));
    if (!e) {
        return -1;
    }
    e->cl   = cl;
    e->next = n->subscribers;
    n->subscribers = e;

    // allocate subscription back‐reference
    sub_ref_t *r = malloc(sizeof(*r));
    if (!r) {
        // rollback the first link
        n->subscribers = e->next;
        free(e);
        return -1;
    }
    r->node = n;
    r->next = cl->subscriptions;
    cl->subscriptions = r;

    return 0;
}

// PUBLIC: subscribe client to pattern (e.g. "a/+/b/*")
int trie_subscribe(client_t *cl, const char *pattern) {
    trie_init();

    // duplicate the pattern so strtok() can modify it
    char *dup = strdup(pattern);
    if (!dup) {
        perror("strdup");
        return -1;
    }

    // split on '/'
    char *parts[64];
    int np = 0;
	char *tok;
    for (tok = strtok(dup, "/");
         tok && np < 64;
         tok = strtok(NULL, "/"))
    {
        parts[np++] = tok;
    }
    if (tok) {
        // too many segments
        fprintf(stderr, "Pattern has more than %d segments\n", 64);
        free(dup);
        return -1;
    }

    // walk/create the path in the trie
    topic_node_t *cur = root;
    for (int i = 0; i < np; i++) {
        topic_node_t *next = NULL;

        if (strcmp(parts[i], "+") == 0) {
            if (!cur->plus_child) {
                next = node_create(cur, CHILD_PLUS, NULL);
                if (!next) {
                    perror("node_create(+)");
                    free(dup);
                    return -1;
                }
                cur->plus_child = next;
            } else {
                next = cur->plus_child;
            }

        } else if (strcmp(parts[i], "*") == 0) {
            if (!cur->star_child) {
                next = node_create(cur, CHILD_STAR, NULL);
                if (!next) {
                    perror("node_create(*)");
                    free(dup);
                    return -1;
                }
                cur->star_child = next;
            } else {
                next = cur->star_child;
            }

        } else {
            next = get_or_create_child(cur, parts[i]);
            if (!next) {
                fprintf(stderr, "get_or_create_child failed for \"%s\"\n", parts[i]);
                free(dup);
                return -1;
            }
        }

        cur = next;
    }

    // attach subscriber
    int add_ret = node_add_subscriber(cur, cl);
    free(dup);

    if (add_ret != 0) {
        fprintf(stderr, "node_add_subscriber failed\n");
        return -1;
    }

    return 0;
}

// helper: remove one client_list_t entry from n->subscribers
static int remove_subscriber_from_node(topic_node_t *n, client_t *cl) {
    client_list_t **prev = &n->subscribers;
    for (client_list_t *e = n->subscribers; e; prev = &e->next, e = e->next) {
        if (e->cl == cl) {
            // unlink and free this entry
            *prev = e->next;
            free(e);

            // prune the trie if this node is now empty
            node_remove_if_empty(n);

            return 0;
        }
    }
    return -1;
}

// PUBLIC: unsubscribe client from exactly this pattern
int trie_unsubscribe(client_t *cl, const char *pattern) {
    if (!root) {
        return -1;
    }

    // duplicate the pattern for strtok
    char *dup = strdup(pattern);
    if (!dup) {
        perror("strdup");
        return -1;
    }

    // split on '/'
    char *parts[64];
    int np = 0;
    char *tok;
    for (tok = strtok(dup, "/");
         tok && np < 64;
         tok = strtok(NULL, "/"))
    {
        parts[np++] = tok;
    }
    if (tok) {
        fprintf(stderr, "Pattern has more than %d segments\n", 64);
        free(dup);
        return -1;
    }

    // walk the trie (but don't create new nodes)
    topic_node_t *cur = root;
    for (int i = 0; i < np; i++) {
        if (strcmp(parts[i], "+") == 0) {
            cur = cur->plus_child;
        } else if (strcmp(parts[i], "*") == 0) {
            cur = cur->star_child;
        } else {
            struct child *found = NULL;
            for (struct child *c = cur->children; c; c = c->next) {
                if (strcmp(c->name, parts[i]) == 0) {
                    found = c;
                    break;
                }
            }
            cur = found ? found->node : NULL;
        }
        if (!cur) {
            // pattern not in trie
            free(dup);
            return -1;
        }
    }
    free(dup);

    // remove the client from this node's subscriber list
    if (remove_subscriber_from_node(cur, cl) < 0) {
        fprintf(stderr, "remove_subscriber_from_node failed\n");
        return -1;
    }

    // remove the corresponding back‐reference from cl->subscriptions
    sub_ref_t **rprev = &cl->subscriptions;
    int found_ref = 0;
    for (sub_ref_t *r = cl->subscriptions; r; r = r->next) {
        if (r->node == cur) {
            *rprev = r->next;
            free(r);
            found_ref = 1;
            break;
        }
        rprev = &r->next;
    }
    if (!found_ref) {
        fprintf(stderr,
                "Warning: subscription reference for '%s' not found\n",
                pattern);
    }

    return 0;
}

// recursive collect for publish
static void collect(topic_node_t *n, char **T, int N, int idx,
                    client_list_t **out)
{
    if (!n) return;

    // '*' at this node can match zero levels
    if (n->star_child) {
        for (client_list_t *e = n->star_child->subscribers; e; e = e->next) {
            client_list_t *c = malloc(sizeof(*c));
            c->cl   = e->cl;
            c->next = *out;
            *out    = c;
        }
        // or eat levels
        for (int j = idx; j < N; j++)
            collect(n->star_child, T, N, j, out);
    }

    if (idx == N) {
        for (client_list_t *e = n->subscribers; e; e = e->next) {
            client_list_t *c = malloc(sizeof(*c));
            c->cl   = e->cl;
            c->next = *out;
            *out    = c;
        }
        return;
    }

    // exact children
    for (struct child *c = n->children; c; c = c->next) {
        if (strcmp(c->name, T[idx]) == 0)
            collect(c->node, T, N, idx+1, out);
    }
    // '+' wildcard
    if (n->plus_child)
        collect(n->plus_child, T, N, idx+1, out);
}

// PUBLIC: publish into the trie
void trie_publish(const char *topic, const char *buf, size_t len) {
    trie_init();
    char *dup = strdup(topic);
    char *T[64]; int N=0;
    for (char *tok = strtok(dup, "/");
         tok && N<64;
         tok = strtok(NULL, "/"))
    {
        T[N++] = tok;
    }

    // collect matches
    client_list_t *raw = NULL;
    collect(root, T, N, 0, &raw);
    free(dup);

    // dedupe & send
    client_t *seen[1024]; int sc=0;
    for (client_list_t *e = raw; e; e = e->next) {
        client_t *cl = e->cl;
        int dup = 0;
        for (int i=0; i<sc; i++) if (seen[i]==cl) { dup=1; break; }
        if (!dup && sc<1024) {
            seen[sc++] = cl;
            send_message(cl->fd, MSG_PUBLISH, buf, len);
        }
    }
    // free raw list
    while (raw) {
        client_list_t *n = raw->next;
        free(raw);
        raw = n;
    }
}

// PUBLIC: on client destroy, remove all its subs cleanly
void cleanup_client_subscriptions(client_t *cl) {
    sub_ref_t *r = cl->subscriptions;
    while (r) {
        sub_ref_t *n = r->next;
        remove_subscriber_from_node(r->node, cl);
        free(r);
        r = n;
    }
    cl->subscriptions = NULL;
}
