#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zmq.h>
#include "jsmn/jsmn.h"

#define HEARTBEAT_INTERVAL 5
#define CONFIG_MAX_PUBURL_SIZE 5
#define JSMN_MAX_TOKEN 32
#define XMR_CONFIG_PATH "/etc/xmr/config.json"

volatile sig_atomic_t sig_status = 0;

typedef struct {
    char *responder_url;
    char **publisher_url;
    int debug;
    int responder_ipv6;
    int publisher_ipv6;

    int _pub_size; /* the size/count of publisher_url array */
} xmr_config_t;

static void handle_signal (int signum) {
    sig_status = signum;
}

static size_t file_get_size (FILE *file) {
    size_t pos, len;

    len = -1;
    if (file && (pos = ftell(file)) != -1) {
        len = !fseek(file, 0, SEEK_END) ? ftell(file) : -1;
        fseek(file, pos, SEEK_SET);
    }

    return len;
}

static char *file_get_string (const char *path) {
    FILE *file;
    size_t i;
    size_t len;
    int c;
    char *data;

    i = 0;
    data = NULL;
    file = fopen(path, "r");
    if (file) {
        len = file_get_size(file);

        if (len > 0)
            data = malloc((len + 1) * sizeof(char));

        if (data) {
            for (;i < len && (c = fgetc(file)) != EOF; data[i++] = c);
            data[len] = 0;
        }

        fclose(file);
    }

    return data;
}

static char *recv_str (void *socket, int *msg_len) {
    zmq_msg_t msg;
    int len;
    int rc;
    char *str;

    *msg_len = 0;

    rc = zmq_msg_init(&msg);
    if (rc != 0) return NULL;

    len = zmq_msg_recv(&msg, socket, 0);
    if (len == -1) return NULL;

    str = malloc((len + 1) * sizeof(char));
    if (str == NULL) return NULL;

    memcpy(str, zmq_msg_data(&msg), len);
    str[len] = 0;

    rc = zmq_msg_close(&msg);
    if (rc != 0) {
        free(str);
        str = NULL;
    }
    *msg_len = len;

    return str;
}

static int jsmn_equal (const char *src, jsmntok_t *tok, const char *str) {
    return (tok->type == JSMN_STRING &&
            (int) strlen(str) == tok->end - tok->start &&
            !strncmp(src + tok->start, str, tok->end - tok->start));
}

static int jsmn_true (const char *src, jsmntok_t *tok) {
    return (tok->type == JSMN_PRIMITIVE &&
            4 == tok->end - tok->start &&
            !strncmp(src + tok->start, "true", tok->end - tok->start));
}

static int send_multipart (void *socket, const char **src) {
    int sent = 0;

    if (src == NULL) return 0;

    sent += zmq_send(socket, src[0], strlen(src[0]), ZMQ_SNDMORE);
    sent += zmq_send(socket, src[1], strlen(src[1]), ZMQ_SNDMORE);
    sent += zmq_send(socket, src[2], strlen(src[2]), 0);

    return sent;
}

static void xmr_config_free (xmr_config_t **config) {
    int i;
    int c;

    if (config && *config) {
        c = (*config)->_pub_size;

        free((*config)->responder_url);
        (*config)->responder_url = NULL;

        for (i = 0; i < c; i++) {
            free((*config)->publisher_url[i]);
            (*config)->publisher_url[i] = NULL;
        }

        free((*config)->publisher_url);
        (*config)->publisher_url = NULL;

        free(*config);
        *config = NULL;
    }
}

static xmr_config_t *default_config () {
    xmr_config_t *config = NULL;
    const char *resp_url = "tcp://127.0.0.1:50001";
    const char *pub_url = "tcp://*:9505";

    config = malloc(sizeof(xmr_config_t));
    config->debug = 0;
    config->publisher_ipv6 = 0;
    config->responder_ipv6 = 0;
    config->publisher_url = malloc(sizeof(char *));
    config->publisher_url[0] = malloc((strlen(pub_url) + 1) * sizeof(char));
    strcpy(config->publisher_url[0], pub_url);
    config->responder_url = malloc((strlen(resp_url) + 1) * sizeof(char));
    strcpy(config->responder_url, resp_url);
    config->_pub_size = 1;

    return config;
}

static xmr_config_t *read_config (const char *src) {
    int i;
    int j;
    int num_token;
    jsmn_parser parser;
    jsmntok_t tokens[JSMN_MAX_TOKEN];
    jsmntok_t *token = NULL;
    int len;
    xmr_config_t *config = NULL;


    if (src == NULL) return default_config();

    config = malloc(sizeof(xmr_config_t));
    config->debug = 0;
    config->publisher_ipv6 = 0;
    config->responder_ipv6 = 0;
    config->publisher_url = NULL;
    config->responder_url = NULL;
    config->_pub_size = 0;

    jsmn_init(&parser);
    num_token = jsmn_parse(&parser, src, strlen(src), tokens, JSMN_MAX_TOKEN);
    if (num_token < 0) return NULL;
    if (num_token < 1 || tokens[0].type != JSMN_OBJECT) return NULL;
    if (num_token > JSMN_MAX_TOKEN) return NULL;

    for (i = 1; i < num_token; i++) {
        len = tokens[i + 1].end - tokens[i + 1].start;

        if (jsmn_equal(src, &tokens[i], "listenOn") && tokens[i + 1].type == JSMN_STRING) {
            config->responder_url = malloc((len + 1) * sizeof(char));
            memcpy(config->responder_url, src + tokens[i + 1].start, len);
            config->responder_url[len] = 0;
            i++;
        }
        else if (jsmn_equal(src, &tokens[i], "pubOn") && tokens[i + 1].type == JSMN_ARRAY) {
            config->_pub_size = tokens[i + 1].size;
            if (config->_pub_size > CONFIG_MAX_PUBURL_SIZE)
                config->_pub_size = CONFIG_MAX_PUBURL_SIZE;

            if (config->_pub_size > 0)
                config->publisher_url = malloc(config->_pub_size * sizeof(char *));

            for (j = 0; j < config->_pub_size; j++) {
                token = &tokens[i + j + 2];
                len = token->end - token->start;
                config->publisher_url[j] = malloc((len + 1) * sizeof(char));
                memcpy(config->publisher_url[j], src + token->start, len);
                config->publisher_url[j][len] = 0;
            }
            i += config->_pub_size + 1;
        }
        else if (jsmn_equal(src, &tokens[i], "debug") && tokens[i + 1].type == JSMN_PRIMITIVE) {
            config->debug = jsmn_true(src, &tokens[i + 1]);
            i++;
        }
        else if (jsmn_equal(src, &tokens[i], "ipv6RespSupport") && tokens[i + 1].type == JSMN_PRIMITIVE) {
            config->responder_ipv6 = jsmn_true(src, &tokens[i + 1]);
            i++;
        }
        else if (jsmn_equal(src, &tokens[i], "ipv6PubSupport") && tokens[i + 1].type == JSMN_PRIMITIVE) {
            config->publisher_ipv6 = jsmn_true(src, &tokens[i + 1]);
            i++;
        }
    }

    return config;
}

static char **jsonstr_to_array (const char *src) {
    int i;
    /* token count found by jsmn_parse from a json encoded string */
    int num_token;
    jsmn_parser parser;
    jsmntok_t tokens[JSMN_MAX_TOKEN];
    /* token length: chars count in a token */
    int len;
    /* return */
    char **str = NULL;


    jsmn_init(&parser);
    num_token = jsmn_parse(&parser, src, strlen(src), tokens, JSMN_MAX_TOKEN);
    if (num_token < 0) return NULL;
    if (num_token < 1 || tokens[0].type != JSMN_OBJECT) return NULL;
    if (num_token > JSMN_MAX_TOKEN) return NULL;

    str = malloc(3 * sizeof(char *));

    for (i = 1; i < num_token; i++) {
        len = tokens[i + 1].end - tokens[i + 1].start;

        if (jsmn_equal(src, &tokens[i], "channel") && tokens[i + 1].type == JSMN_STRING) {
            str[0] = malloc((len + 1) * sizeof(char));
            memcpy(str[0], src + tokens[i + 1].start, len);
            str[0][len] = 0;
        }
        else if (jsmn_equal(src, &tokens[i], "message") && tokens[i + 1].type == JSMN_STRING) {
            str[1] = malloc((len + 1) * sizeof(char));
            memcpy(str[1], src + tokens[i + 1].start, len);
            str[1][len] = 0;
        }
        else if (jsmn_equal(src, &tokens[i], "key") && tokens[i + 1].type == JSMN_STRING) {
            str[2] = malloc((len + 1) * sizeof(char));
            memcpy(str[2], src + tokens[i + 1].start, len);
            str[2][len] = 0;
        }
    }

    return str;
}

int main () {
    time_t hb_time;
    int rc;
    int i;
    /* contents of config.json */
    char *config_str = NULL;
    xmr_config_t *config;

    void *context = zmq_ctx_new();
    /* sockets */
    void *responder = zmq_socket(context, ZMQ_REP);
    void *publisher = zmq_socket(context, ZMQ_PUB);

    const char *heartbeat_msg[3] = { "H", "", "" };
    zmq_pollitem_t items[1];

    config_str = file_get_string(XMR_CONFIG_PATH);
    config = read_config(config_str);
    free(config_str);

    zmq_setsockopt(responder, ZMQ_IPV6, &(config->responder_ipv6), sizeof(int));
    zmq_bind(responder, config->responder_url);

    zmq_setsockopt(publisher, ZMQ_IPV6, &(config->publisher_ipv6), sizeof(int));
    for (i = 0; i < config->_pub_size; i++) {
        zmq_bind(publisher, config->publisher_url[i]);
    }

    /* TODO: config->debug */

    hb_time = time(NULL) + HEARTBEAT_INTERVAL;
    signal(SIGINT, handle_signal);

    while (1) {
        /* json encoded message sent by cms */
        char *msg = NULL;
        int msg_len = 0;
        /* values of channel, message, and key as array of string.
           multipart[0] -> channel,
           multipart[1] -> message,
           multipart[2] -> key       */
        char **multipart = NULL;

        items[0].socket = responder;
        items[0].events = ZMQ_POLLIN;

        rc = zmq_poll(items, 1, HEARTBEAT_INTERVAL * 1000);
        if (sig_status || rc == -1) break;

        if (items[0].revents & ZMQ_POLLIN) {
            msg = recv_str(responder, &msg_len);
            if (msg != NULL) {
                multipart = jsonstr_to_array(msg);
                if (multipart == NULL || multipart[0] == NULL ||
                        multipart[1] == NULL || multipart[2] == NULL) {
                    free(msg);
                    continue;
                }
                /* send ack response */
                zmq_send(responder, "1", 1, 0);

                send_multipart(publisher, (const char **) multipart);

                free(multipart[0]);
                free(multipart[1]);
                free(multipart[2]);
                free(multipart);
                free(msg);
            }
        }
        if (hb_time <= time(NULL)) {
            /* approximate next heartbeat time */
            hb_time += HEARTBEAT_INTERVAL;
            /* more precise next heartbeat time */
            /* hb_time = time(NULL) + HEARTBEAT_INTERVAL; */

            send_multipart(responder, heartbeat_msg);
        }
    }

    xmr_config_free(&config);

    zmq_close(responder);
    zmq_close(publisher);
    zmq_ctx_term(context);

    return 0;
}
