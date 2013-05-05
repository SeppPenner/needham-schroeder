/**
 * simple Needham-Schroeder implementation
 *
 * Copyright (c) 2013 Andreas Bender <bender@tzi.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
 
/**
 * TODOs
 *  - Implement session multiplexing so multiple clients can connect to a daemon
 *      at the same time.
 *  - Implement retransmissions to handle lossy networks
 *  - Compatibility with Contiki-OS
 */

#ifndef _NEEDHAM_SCHROEDER_H_
#define _NEEDHAM_SCHROEDER_H_

#include <sys/types.h>
#include <netinet/in.h>

#include "util.h"
#include "uthash.h"

/* Log levels:
   0: debug
   1: info
   2: warning
   3: error
   4: fatal */

/* CAUTION: The library currently only supports multiples of 16 for NS_KEY_LENGTH,
 * NS_IDENTITY_LENGTH and NS_NONCE_LENGTH.
 * 
 * NS_RIN_KEY_LENGTH MUST be 16 for the currently used rijndael implementation.
 *
 * All values in Bytes
 */
#define NS_KEY_LENGTH 16      // Length of the tmp-key used for DTLS
#define NS_RIN_KEY_LENGTH 16  // Length of the AES Key
#define NS_IDENTITY_LENGTH 16 // Length of global identifiers for communication partners
#define NS_NONCE_LENGTH 16    // Nonce length used in Needham-Schroeder

/* Message codes */
typedef enum {
  NS_STATE_INITIAL = 0,
  NS_STATE_KEY_REQUEST,
  NS_STATE_KEY_RESPONSE,
  NS_STATE_COM_REQUEST,
  NS_STATE_COM_CHALLENGE,
  NS_STATE_COM_RESPONSE,
  NS_STATE_FINISHED
} ns_state_t;

/* Message error codes */
typedef enum {
  NS_ERR_UNKNOWN_ID = 17,
  NS_ERR_REJECTED,
  NS_ERR_UNKNOWN
} ns_error_t;

/* IPv4/IPv6 Address abstraction */
typedef struct {
  socklen_t size;
  union {
    struct sockaddr     sa;
    struct sockaddr_storage st;
    struct sockaddr_in  sin;
    struct sockaddr_in6 sin6;
  } addr;
} ns_abstract_address_t;

/**
 * These callbacks are used to provide an interface for the server so the user
 * may decide how the server stores/retrieves keys. (in memory, in a database,...)
 */
typedef struct {
  
  /**
   * Callback to find an key for \p identity_name and store its key in \p key.
   *
   * @param identity_name Pointer to a zero-terminated string, containing the
   *       name.
   * @param key The library expects an array of NS_RIN_KEY_LENGTH size and the
   *       key of the identity stored here.
   *
   * @return An integer with the following codes:
   *  -1 : identity not found
   *   0 : success
   */
  int (*get_key)(char *identity_name, char *key);
  
} ns_server_handler_t;

/**
 * These callbacks are used to interface the client. The user may decide how
 * to store/retrieve keys and how to react on process completion.
 */
typedef struct {
  
  /**
   * Callback to store an identity name and its corresponding key.
   *
   * @return
   *  -1 : On any error
   *   0 : success
   */
  int (*store_key)(char *identity_name, char *key);
  
  /**
   * Callback to get a key for \p identity_name. The key must be placed in
   * \p key.
   *
   * @return
   *   0 : success
   *  -1 : on any error, identity not found, key not found etc.
   */
  int (*get_key)(char *identity_name, char *key);
  
  /**
   * Callback when the client finishes the process, either on completion or
   * on error.
   *
   * @return The state at which the process finished. See "ns_state_t" and
   *     "ns_error_t" for possible codes.
   */
  int (*result)(int code);
  
} ns_client_handler_t;

/**
 * These callbacks are used to interface the daemon, which is waiting for client
 * connections.
 */
typedef struct {

  /**
   * Callback to store an identity name and its corresponding key.
   *
   * @return
   *  -1 : On any error
   *   0 : success
   */
  int (*store_key)(char *identity_name, char *key);
  
  /**
   * Callback to get a key for \p identity_name. The key must be placed in
   * \p key.
   *
   * @return
   *   0 : success
   *  -1 : on any error, identity not found, key not found etc.
   */
  int (*get_key)(char *identity_name, char *key);
  
} ns_daemon_handler_t;

typedef struct {
  ns_client_handler_t *handler;
  int state;
  int socket;
  ns_abstract_address_t *server_addr;
  ns_abstract_address_t *peer_addr;
  char client_ns_key[NS_RIN_KEY_LENGTH];
  char peer_identity[NS_IDENTITY_LENGTH];
} ns_client_context_t;

typedef struct {
  UT_hash_handle hh;
  ns_abstract_address_t addr;
  char nonce[NS_NONCE_LENGTH];
} ns_daemon_peer_t;

typedef struct {
  ns_daemon_handler_t *handler;
  int socket;
  char daemon_ns_key[NS_RIN_KEY_LENGTH];
  char peer_key[NS_KEY_LENGTH];
  ns_daemon_peer_t *peers;
} ns_daemon_context_t;

/**
 * Start a needham-schroeder server. The server is waiting for key requests by
 * the client and answers these with newly generated keys.
 *
 * @param handler Callbacks for the server. See struct description.
 * @param port The port the server should listen on
 */
void ns_server(ns_server_handler_t *handler, int port);

/**
 * Start a needham-schroeder daemon, waiting for communication requests by a
 * client.
 *
 * @param handler Callbacks for the daemon. See struct description.
 * @param port The port the server should listen on
 * @param key The daemons key as stored in the server
 */
void ns_daemon(ns_daemon_handler_t *handler, int port, char *key);

/**
 * Starts a client which will try to get a new key from the server to
 * securely communicate with some peer.
 *
 * @param handler Callbacks for the client. See struct description.
 * @param server_address A string containing the servers address. Either as
 *       Domain name or resolved IP-address.
 * @param partner_address The address of the peer the client wants to talk to.
 * @param server_port The port of the server
 * @param client_port The port the client will open for its communication
 * @param partner_port The port of the peer
 * @param client_identity The identity name of the client as zero-terminated
 *       string.
 * @param partner_identity The identity name of the peer.
 * @param key The key of the client for the needham-schroeder process.
 */
int ns_get_key(ns_client_handler_t handler,
      char *server_address, char *partner_address, 
      int server_port, int client_port, int partner_port,
      char *client_identity, char *partner_identity, char *key);

#endif // _NEEDHAM_SCHROEDER_H_