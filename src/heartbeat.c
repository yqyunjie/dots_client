//
// Created by Yiping Deng on 4/11/20.
//

#include "heartbeat.h"
#include <cbor.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "log.h"
#include "task_env.h"
#include "preconditions.h"
#include "utils.h"

#define CBOR_HEARTBEAKT_KEY 49
#define CBOR_PEER_HB_STATUS_KEY 51

static const char *const HB_REQUEST_PATH = ".well-known/dots/hb";
static pthread_t heartbeat_thread = NULL;

/**
 *      Header: PUT (Code=0.03)
        Uri-Path: ".well-known"
        Uri-Path: "dots"
        Uri-Path: "hb"
        Content-Format: "application/dots+cbor"

        {
          "ietf-dots-signal-channel:heartbeat": {
             "peer-hb-status": true
           }
        }

   The DOTS Heartbeat mechanism uses non-confirmable PUT requests
   (Figure 27) with an expected 2.04 (Changed) Response Code
   (Figure 28).  This procedure occurs between a DOTS agent and its
   immediate peer DOTS agent.  As such, this PUT request MUST NOT be
   relayed by a DOTS gateway.  The PUT request used for DOTS heartbeat
   MUST NOT have a 'cuid', 'cdid,' or 'mid' Uri-Path.
 */
static void create_cbor_heartbeat(uint8_t **buffer_ptr, size_t *len_ptr) {
    cbor_item_t *nested = cbor_new_definite_map(1);
    cbor_map_add(nested, (struct cbor_pair) {
            .key = cbor_move(cbor_build_uint32(CBOR_PEER_HB_STATUS_KEY)),
            .value = cbor_move(cbor_build_bool(true))
    });
    cbor_item_t *root = cbor_new_definite_map(1);
    cbor_map_add(root, (struct cbor_pair) {
            .key = cbor_move(cbor_build_uint32(CBOR_HEARTBEAKT_KEY)),
            .value = cbor_move(nested)
    });
    cbor_serialize_alloc(root, buffer_ptr, len_ptr);
    cbor_decref(&root);
}

int validate_cbor_heartbeat_body(uint8_t *buffer, size_t len) {
    struct cbor_load_result result;
    cbor_item_t *item = cbor_load(buffer, len, &result);

    if (log_get_level() <= LOG_LEVEL_DEBUG) {
        cbor_describe(item, stdout);
    }

    int map_size = cbor_map_size(item);
    if (map_size != 1) {
        log_error("Heartbeat is invalid! Top level map size is %i, expecting 1!", map_size);
        cbor_decref(&item);
        return 0;
    }

    struct cbor_pair *heartbeat_pair = cbor_map_handle(item);
    int heartbeat_key = cbor_get_int(heartbeat_pair->key);
    if (heartbeat_key != CBOR_HEARTBEAKT_KEY) {
        log_error("Heartbeat has key of %i, expecting %i", heartbeat_key, CBOR_HEARTBEAKT_KEY);
        cbor_decref(&item);
        return 0;
    }

    map_size = cbor_map_size(heartbeat_pair->value);
    if (map_size != 1) {
        log_error("Heartbeat is invalid! Nested map inside of heartbeat has size %i, expecting 1!", map_size);
        cbor_decref(&item);
        return 0;
    }

    struct cbor_pair *status_pair = cbor_map_handle(heartbeat_pair->value);
    if (cbor_get_int(status_pair->key) != CBOR_PEER_HB_STATUS_KEY || cbor_get_bool(status_pair->value) != true) {
        log_error("Peer hb status doesn't exists or it is not true!");
        cbor_decref(&item);
        return 0;
    }

    cbor_decref(&item);
    return 1;
}

static void heartbeat_send(dots_task_env *env) {
    log_debug("Sending a heartbeat!");
    uint8_t *buffer;
    size_t buffer_len;
    create_cbor_heartbeat(&buffer, &buffer_len);
    check_valid(buffer, "Unable to deserialize heartbeat payload");
    uint16_t message_id = coap_new_message_id(env->curr_sess);
    coap_pdu_t *pdu = coap_pdu_init(
            COAP_MESSAGE_NON,
            COAP_REQUEST_PUT,
            message_id,
            coap_session_max_pdu_size(env->curr_sess));
    coap_add_option(pdu, COAP_OPTION_URI_PATH, strlen(HB_REQUEST_PATH), HB_REQUEST_PATH);
    coap_add_option(pdu, COAP_OPTION_CONTENT_TYPE, )
    coap_add_data(pdu, buffer_len, buffer);

    coap_send(env->curr_sess, pdu);
    dots_describe_pdu(pdu);
    /*
    if (coap_get_log_level() <= LOG_DEBUG) {
        coap_show_pdu(LOG_DEBUG, pdu);
    }
    */
    free(buffer);
}

static void active_heartbeat_runnable(dots_task_env *env) {
    while (1) {
        sleep(env->heartbeat_interval);
        log_info("Send out a heartbeat!");
        heartbeat_send(env);
    }
}

void start_heartbeat(dots_task_env *env) {
    if (!heartbeat_thread) {
        log_info("Create heartbeat ticker! It will tick every %i seconds!", env->heartbeat_interval);
        check_valid(
                !pthread_create(&heartbeat_thread, NULL, active_heartbeat_runnable, env),
                "Cannot create a new thread!");
        pthread_detach(heartbeat_thread);
    }
}


