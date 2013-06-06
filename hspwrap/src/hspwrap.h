#ifndef HSPWRAP_H__
#define HSPWRAP_H__

#include <stdint.h>

// Common utils
#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZERO_ARRAY(a) memset(a, 0, sizeof(a))

// Tag for master messages
#define TAG_WORKUNIT  0
#define TAG_DATA      1
#define TAG_MASTERCMD 2
// Tag for slave messages
#define TAG_REQUEST 3

typedef uint32_t blockid_t;
typedef uint16_t blockcnt_t;

// Types of work units (from master to slave)
enum workunit_type {
  WU_TYPE_EXIT,
  WU_TYPE_DATA
};

// Types of requests (from slave to master)
enum request_type {
  REQ_WORKUNIT,
};

// Work unit message
struct workunit {
  enum workunit_type type;
  uint32_t           count;
  uint32_t           len;
  blockid_t          blk_id;
};

// Request message
struct request {
  enum request_type  type;
  uint32_t           count;
};

#endif // HSPWRAP_H__
