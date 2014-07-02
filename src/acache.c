/* acache.c: cache all data messages and respond to requests */
/* only one thread, listening on a pipe from ad, and responding
 * acache takes two arguments, the fd of a pipe from AD and of a pipe to AD
 */
/* for now, simply dcache all the packets, return them if they are in the
 * cache.  Later on, maybe provide persistent storage */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/packet.h"
#include "lib/pipemsg.h"
#include "lib/priority.h"
#include "lib/util.h"
#include "lib/dcache.h"
#include "lib/log.h"
#include "lib/sha.h"

#define CACHE_SIZE	1024
/* #define CACHE_SIZE	4  /* for testing */
struct cache_entry {
  char * message;
  int msize;
};

static struct cache_entry cache_storage [CACHE_SIZE];

static void return_cache_entry (void * arg)
{
  struct cache_entry * cep = (struct cache_entry *) arg;
  free (cep->message);
  cep->msize = 0;   /* mark it as free */
  snprintf (log_buf, LOG_SIZE,
            "cleared msize %p, index %ld = (%p - %p) / %zd\n",
            cep, (long int) ((cep - cache_storage) / sizeof (cache_storage)),
            cep, cache_storage, sizeof (cache_storage));
  log_print ();
  int i;
  for (i = 0; i < CACHE_SIZE; i++) {
    if (cache_storage [i].msize > 0) {
      snprintf (log_buf, LOG_SIZE, "cache %d has msize %d\n",
                i, cache_storage [i].msize);
      log_print ();
    }
  }
}

/* if the header includes an id, returns a pointer to the ID field of hp */
static char * get_id (char * message, int size)
{
  struct allnet_header * hp = (struct allnet_header *) message;
  char * id = ALLNET_PACKET_ID (hp, hp->transport, size);
  if (id == NULL)
    id = ALLNET_MESSAGE_ID (hp, hp->transport, size);
  /* ack messages may have multiple IDs, we only use the first */
  if ((id == NULL) && (hp->message_type == ALLNET_TYPE_ACK) &&
      (size >= ALLNET_SIZE (hp->transport) + MESSAGE_ID_SIZE))
    id = ALLNET_DATA_START (hp, hp->message_type, size);
  /* key messages usually don't have IDs, but do have hmac or fingerprints */
  if ((id == NULL) && (size >= ALLNET_SIZE (hp->transport) + 1)) {
    int nbytes = message [ALLNET_SIZE (hp->transport) + 1] & 0xff;
    if ((size >= ALLNET_SIZE (hp->transport) + 1 + nbytes) &&
        (nbytes >= MESSAGE_ID_SIZE)) {
      if ((hp->message_type == ALLNET_TYPE_KEY_XCHG) ||
          (hp->message_type == ALLNET_TYPE_KEY_REQ))
        id = message + ALLNET_SIZE (hp->transport) + 1;
    }
  }
  return id;  /* a pointer (if any) into hp */ 
}

static int match_packet_id (void * packet_id, void * cache_entry)
{
  struct cache_entry * cep = (struct cache_entry *) cache_entry;
  char * id = get_id (cep->message, cep->msize);
  if ((id != NULL) && (memcmp (id, packet_id, MESSAGE_ID_SIZE) == 0))
    return 1;
  return 0;
}

/* returns 1 if successful, 0 otherwise */
static int save_packet (void * cache, char * message, int msize)
{
  struct allnet_header * hp = (struct allnet_header *) message;
#ifdef DEBUG_PRINT
  snprintf (log_buf, LOG_SIZE, "save_packet: size %d\n", msize);
  log_print ();
#endif /* DEBUG_PRINT */
  char * id = get_id (message, msize);
  if (id == NULL)   /* no sort of message or packet ID found */
    return 0;
  int i;
#ifdef DEBUG_PRINT
  buffer_to_string (id, MESSAGE_ID_SIZE, "id", MESSAGE_ID_SIZE, 1,
                    log_buf + off, LOG_SIZE - off);
  log_print ();
  for (i = 0; i < CACHE_SIZE; i++) {
    if ((cache_storage [i].msize > 0) && (cache_storage [i].message != NULL)) {
      int xoff = snprintf (log_buf, LOG_SIZE,
                           "cache entry %d has msize %d, id ",
                           i, cache_storage [i].msize);
      struct allnet_header * xhp = 
               (struct allnet_header *) cache_storage [i].message;
      char * xid = get_id ((char *) xhp, cache_storage [i].msize);
      buffer_to_string (xid, MESSAGE_ID_SIZE, NULL, 8, 1,
                        log_buf + xoff, LOG_SIZE - xoff);
      log_print ();
    }
  }
#endif /* DEBUG_PRINT */
  if ((cache_get_match (cache, match_packet_id, id)) != NULL) {
    /* already in cache */
    snprintf (log_buf, LOG_SIZE, "packet found in cache, not saving\n");
    log_print ();
    return 0;
  }
  static int cache_search = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    if (cache_storage [(i + cache_search) % CACHE_SIZE].msize == 0) {
      /* found a free slot -- since the dcache has CACHE_SIZE - 1 entries,
         there should always be a free slot. */
      snprintf (log_buf, LOG_SIZE, "saving message of type %d, %d bytes\n",
                hp->message_type, msize);
      log_print ();
      struct cache_entry * cep = 
        cache_storage + ((i + cache_search) % CACHE_SIZE);
      cache_search = i;
      cep->message = message;
      cep->msize = msize;
      cache_add (cache, cep);  /* may call return_cache_entry */
      return 1;
    }
  }
  free (message);
  snprintf (log_buf, LOG_SIZE,
            "unable to find a free slot!!!  message not saved\n");
  log_print ();
  return 0;
}

static int request_matches_packet (void * packet, void * cache_entry)
{
  struct allnet_header * hp = (struct allnet_header *) packet;
  struct cache_entry * cep = (struct cache_entry *) cache_entry;
  struct allnet_header * chp = (struct allnet_header *) (cep->message);
  if ((hp->src_nbits > ADDRESS_BITS) || (chp->dst_nbits > ADDRESS_BITS))
    return 0;
  if (matches (hp->source, hp->src_nbits, chp->destination, chp->dst_nbits))
    return 1;
  return 0;
}

/* returns 1 if it sent a response, 0 otherwise */
static int respond_to_packet (void * cache, char * message,
                              int msize, int fd)
{
  struct allnet_header * hp = (struct allnet_header *) message;
  void * * matches;
  int nmatches = 
    cache_all_matches (cache, request_matches_packet, message, &matches);
  if (nmatches == 0)
    return 0;
  snprintf (log_buf, LOG_SIZE, "respond_to_packet: %d matches\n", nmatches);
  log_print ();
  struct cache_entry * * cep = (struct cache_entry * *) matches;
  int i;
  for (i = 0; i < nmatches; i++) {
    snprintf (log_buf, LOG_SIZE,
              "sending %d-byte cached response\n", cep [i]->msize);
    log_print ();
    /* send, no need to even check the return value of send_pipe_message */
    send_pipe_message (fd, cep [i]->message, cep [i]->msize,
                       ALLNET_PRIORITY_CACHE_RESPONSE);
  }
  free (matches);
  return 1;
}

static void ack_packets (void * cache, char * message, int msize)
{
  struct allnet_header * hp = (struct allnet_header *) message;
  char * ack = ALLNET_DATA_START (hp, hp->transport, msize);
  msize -= (ack - message);
  while (msize >= MESSAGE_ID_SIZE) {
    char hash [MESSAGE_ID_SIZE];
    sha512_bytes (ack, MESSAGE_ID_SIZE, hash, MESSAGE_ID_SIZE);
    void * found;
    while ((found = cache_get_match (cache, match_packet_id, hash)) != NULL)
      cache_remove (cache, found);
    ack += MESSAGE_ID_SIZE;
    msize -= MESSAGE_ID_SIZE;
  }
}

void * main_loop (int sock)
{
  bzero (cache_storage, sizeof (cache_storage));
  void * cache = cache_init (CACHE_SIZE - 1, return_cache_entry);
  while (1) {
    char * message;
    int priority;
    int result = receive_pipe_message (sock, &message, &priority);
    struct allnet_header * hp = (struct allnet_header *) message;
    /* unless we save it, free the message */
    int mfree = 1;
    if (result <= 0) {
      snprintf (log_buf, LOG_SIZE, "ad pipe %d closed, result %d\n",
                sock, result);
      log_print ();
      mfree = 0;  /* is this ever needed or useful? */
      break;
    } else if ((result >= ALLNET_HEADER_SIZE) &&
               (result >= ALLNET_SIZE (hp->transport))) {
      /* valid message from ad: save, respond, or ignore */
      if (hp->message_type == ALLNET_TYPE_DATA_REQ) { /* respond */
        if (respond_to_packet (cache, message, result, sock))
          snprintf (log_buf, LOG_SIZE, "responded to data request packet\n");
        else
          snprintf (log_buf, LOG_SIZE, "no response to data request packet\n");
      } else {   /* not a data request */
        if (hp->message_type == ALLNET_TYPE_ACK) /* erase if have */
          ack_packets (cache, message, result);
        if (save_packet (cache, message, result)) {
          mfree = 0;   /* saved, so do not free */
          snprintf (log_buf, LOG_SIZE, "saved packet of type %d, size %d\n",
                    hp->message_type, result);
        } else {
          snprintf (log_buf, LOG_SIZE,
                    "did not save packet, type %d, size %d\n",
                    hp->message_type, result);
        }
      }
      log_print ();
    } else {
      snprintf (log_buf, LOG_SIZE, "ignoring packet of size %d\n", result);
      log_print ();
    }
    if (mfree)
      free (message);
  }
}

int main (int argc, char ** argv)
{
  int sock = connect_to_local ("acache", argv [0]);
  main_loop (sock);
  snprintf (log_buf, LOG_SIZE, "end of acache\n");
  log_print ();
}
