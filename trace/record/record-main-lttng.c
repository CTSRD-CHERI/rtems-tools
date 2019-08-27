/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Ravindra Kumar Meena <rmeena840@gmail.com>
 * Copyright (C) 2018, 2019 embedded brains GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <rtems/recorddata.h>
#include <rtems/recordclient.h>

#include <sys/queue.h>
#include <sys/socket.h>

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <errno.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define CTF_MAGIC                 0xC1FC1FC1
#define TASK_RUNNING              0x0000
#define TASK_IDLE                 0x0402
#define UUID_SIZE                 16
#define THREAD_NAME_SIZE          16
#define THREAD_API_COUNT          3
#define THREAD_ID_COUNT           0x10000
#define BITS_PER_CHAR             8
#define COMPACT_HEADER_ID         31

static const struct option longopts[] = {
  { "help", 0, NULL, 'h' },
  { "host", 1, NULL, 'H' },
  { "port", 1, NULL, 'p' },
  { "input", 1, NULL, 'i' },
  { NULL, 0, NULL, 0 }
};

typedef struct {
  uint64_t           ns;
  uint32_t           cpu;
  rtems_record_event event;
  uint64_t           data;
} client_item;

typedef struct {
  uint32_t                     ctf_magic;
  uint8_t                      uuid[ UUID_SIZE ];
  uint32_t                     stream_id;
  uint64_t                     stream_instance_id;
} __attribute__((__packed__)) packet_header;

typedef struct packet_context {
  packet_header                header;
  uint64_t                     timestamp_begin;
  uint64_t                     timestamp_end;
  uint64_t                     content_size;
  uint64_t                     packet_size;
  uint64_t                     packet_seq_num;
  unsigned long                events_discarded;
  uint32_t                     cpu_id;
} __attribute__((__packed__)) packet_context;

typedef struct {
  uint8_t                      id;
  uint32_t                     event_id;
  uint64_t                     ns;
} __attribute__((__packed__)) event_header_compact;

typedef struct {
  event_header_compact         header;
  uint8_t                      prev_comm[ THREAD_NAME_SIZE ];
  int32_t                      prev_tid;
  int32_t                      prev_prio;
  int64_t                      prev_state;
  uint8_t                      next_comm[ THREAD_NAME_SIZE ];
  int32_t                      next_tid;
  int32_t                      next_prio;
} __attribute__((__packed__)) sched_switch;

typedef struct {
  FILE         *event_stream;
  uint64_t      timestamp_begin;
  uint64_t      timestamp_end;
  uint64_t      content_size;
  uint64_t      packet_size;
  uint32_t      thread_id;
  uint64_t      thread_ns;
  size_t        thread_name_index;
  sched_switch  sched_switch;
} per_cpu_context;

typedef struct {
  per_cpu_context per_cpu[ RTEMS_RECORD_CLIENT_MAXIMUM_CPU_COUNT ];

  /*
   * @brief Thread names indexed by API and object index.
   *
   * The API indices are 0 for Internal API, 1 for Classic API and 2 for
   * POSIX API.
   */
  char thread_names[ THREAD_API_COUNT ][ THREAD_ID_COUNT ][ THREAD_NAME_SIZE ];
} client_context;

static const char empty_thread_name[ THREAD_API_COUNT ];

static const uint8_t uuid[] = { 0x6a, 0x77, 0x15, 0xd0, 0xb5, 0x02, 0x4c, 0x65,
0x86, 0x78, 0x67, 0x77, 0xac, 0x7f, 0x75, 0x5a };

static void usage( char **argv )
{
  printf(
    "%s [--host=HOST] [--port=PORT] [--input=INPUT]\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "  -h, --help                 print this help text\n"
    "  -H, --host=HOST            the host IPv4 address of the record server\n"
    "  -p, --port=PORT            the TCP port of the record server\n"
    "  -i, --input=INPUT          the input file\n",
    argv[ 0 ]
  );
}

static int connect_client( const char *host, uint16_t port,
const char *input_file, bool input_file_flag )
{
  struct sockaddr_in in_addr;
  int fd;
  int rv;

  fd = ( input_file_flag ) ? open( input_file, O_RDONLY ) :
  socket( PF_INET, SOCK_STREAM, 0 );
  assert( fd >= 0 );

  memset( &in_addr, 0, sizeof( in_addr ) );
  in_addr.sin_family = AF_INET;
  in_addr.sin_port = htons( port );
  in_addr.sin_addr.s_addr = inet_addr( host );

  if ( !input_file_flag ) {
    rv = connect( fd, ( struct sockaddr * ) &in_addr, sizeof( in_addr ) );
    assert( rv == 0 );
  }

  return fd;
}

static uint32_t get_api_index_of_id( uint32_t id )
{
  return ( ( id >> 24 ) & 0x7 ) - 1;
}

static uint32_t get_obj_index_of_id( uint32_t id )
{
  return id & ( THREAD_ID_COUNT - 1 );
}

static bool is_idle_task_by_api_index( uint32_t api_index )
{
  return api_index == 0;
}

static void copy_thread_name(
  const client_context *cctx,
  const client_item    *item,
  size_t                api_index,
  uint8_t              *dst
)
{
  const char *name;

  if ( api_index < THREAD_API_COUNT ) {
    name = cctx->thread_names[ api_index ][ get_obj_index_of_id( item->data ) ];
  } else {
    name = empty_thread_name;
  }

  memcpy( dst, name, THREAD_NAME_SIZE );
}

static void write_sched_switch(
  client_context    *cctx,
  per_cpu_context   *pcpu,
  const client_item *item
)
{
  size_t        se_size;
  sched_switch *se;
  uint32_t      api_index;

  se_size = sizeof( sched_switch ) * BITS_PER_CHAR;
  pcpu->content_size += se_size;
  pcpu->packet_size += se_size;

  api_index = get_api_index_of_id( item->data );
  se = &pcpu->sched_switch;

  se->header.id = COMPACT_HEADER_ID;
  se->header.event_id = 0;
  se->header.ns = item->ns;
  se->next_tid = is_idle_task_by_api_index( api_index ) ? 0 : item->data;

  copy_thread_name( cctx, item, api_index, se->next_comm );
  fwrite( se, sizeof( *se ), 1, pcpu->event_stream );
}

static void add_thread_name(
  client_context    *cctx,
  per_cpu_context   *pcpu,
  const client_item *item
)
{
  uint64_t name;
  uint32_t api_index;
  uint32_t obj_index;
  size_t i;

  if ( pcpu->thread_name_index >= THREAD_NAME_SIZE ) {
    return;
  }

  api_index = get_api_index_of_id( pcpu->thread_id );

  if ( api_index >= THREAD_API_COUNT ) {
    return;
  }

  obj_index = get_obj_index_of_id( pcpu->thread_id );
  name = item->data;

  for (
    i = pcpu->thread_name_index;
    i < pcpu->thread_name_index + 8;
    ++i
  ) {
    cctx->thread_names[ api_index ][ obj_index ][ i ] = (char) name;
    name >>= BITS_PER_CHAR;
  }

  pcpu->thread_name_index = i;
}

static void print_item( client_context *cctx, const client_item *item )
{
  per_cpu_context *pcpu;
  sched_switch    *se;
  uint32_t         api_index;

  pcpu = &cctx->per_cpu[ item->cpu ];
  se = &pcpu->sched_switch;

  if ( pcpu->timestamp_begin == 0 ) {
    pcpu->timestamp_begin = item->ns;
  }

  pcpu->timestamp_end = item->ns;

  switch ( item->event ) {
    case RTEMS_RECORD_THREAD_SWITCH_OUT:
      api_index = get_api_index_of_id( item->data );
      se->header.ns = item->ns;

      if ( is_idle_task_by_api_index( api_index ) ) {
        se->prev_tid = 0;
        se->prev_state = TASK_IDLE;
      } else {
        se->prev_tid = item->data;
        se->prev_state = TASK_RUNNING;
      }

      copy_thread_name( cctx, item, api_index, se->prev_comm );
      break;
    case RTEMS_RECORD_THREAD_SWITCH_IN:
      if ( item->ns == se->header.ns ) {
        write_sched_switch( cctx, pcpu, item );
      }
      break;
    case RTEMS_RECORD_THREAD_ID:
      pcpu->thread_id = item->data;
      pcpu->thread_ns = item->ns;
      pcpu->thread_name_index = 0;
      break;
    case RTEMS_RECORD_THREAD_NAME:
      add_thread_name( cctx, pcpu, item );
      break;
    default:
      break;
  }
}

static rtems_record_client_status handler(
  uint64_t            bt,
  uint32_t            cpu,
  rtems_record_event  event,
  uint64_t            data,
  void               *arg
)
{
  client_item item;

  item.ns = rtems_record_client_bintime_to_nanoseconds( bt );
  item.cpu = cpu;
  item.event = event;
  item.data = data;

  print_item( arg, &item );

  return RTEMS_RECORD_CLIENT_SUCCESS;
}

static const char metadata[] =
"/* CTF 1.8 */\n"
"\n"
"typealias integer { size = 5; align = 1; signed = false; } := uint5_t;\n"
"typealias integer { size = 8; align = 8; signed = false; } := uint8_t;\n"
"typealias integer { size = 32; align = 8; signed = false; } := uint32_t;\n"
"typealias integer { size = 64; align = 8; signed = false; } := uint64_t;\n"
"typealias integer { size = 64; align = 8; signed = false; } := unsigned long;\n"
"\n"
"trace {\n"
"\tmajor = 1;\n"
"\tminor = 8;\n"
"\tuuid = \"6a7715d0-b502-4c65-8678-6777ac7f755a\";\n"
"\tbyte_order = le;\n"
"\tpacket.header := struct {\n"
"\t\tuint32_t magic;\n"
"\t\tuint8_t  uuid[16];\n"
"\t\tuint32_t stream_id;\n"
"\t\tuint64_t stream_instance_id;\n"
"\t};\n"
"};\n"
"\n"
"env {\n"
"\thostname = \"Record_Item\";\n"
"\tdomain = \"kernel\";\n"
"\tsysname = \"Linux\";\n"
"\tkernel_release = \"4.18.14-arch1-1-ARCH\";\n"
"\tkernel_version = \"#1 SMP PREEMPT Sat Thu 17 13:42:37 UTC 2019\";\n"
"\ttracer_name = \"lttng-modules\";\n"
"\ttracer_major = 2;\n"
"\ttracer_minor = 11;\n"
"\ttracer_patchlevel = 0;\n"
"};\n"
"\n"
"clock {\n"
"\tname = \"monotonic\";\n"
"\tuuid = \"234d669d-7651-4bc1-a7fd-af581ecc6232\";\n"
"\tdescription = \"Monotonic Clock\";\n"
"\tfreq = 1000000000;\n"
"\toffset = 1539783991179109789;\n"
"};\n"
"\n"
"typealias integer {\n"
"\tsize = 27; align = 1; signed = false;\n"
"\tmap = clock.monotonic.value;\n"
"} := uint27_clock_monotonic_t;\n"
"\n"
"typealias integer {\n"
"\tsize = 64; align = 8; signed = false;\n"
"\tmap = clock.monotonic.value;\n"
"} := uint64_clock_monotonic_t;\n"
"\n"
"struct packet_context {\n"
"\tuint64_clock_monotonic_t timestamp_begin;\n"
"\tuint64_clock_monotonic_t timestamp_end;\n"
"\tuint64_t content_size;\n"
"\tuint64_t packet_size;\n"
"\tuint64_t packet_seq_num;\n"
"\tunsigned long events_discarded;\n"
"\tuint32_t cpu_id;\n"
"};\n"
"\n"
"struct event_header_compact {\n"
"\tenum : uint5_t { compact = 0 ... 30, extended = 31 } id;\n"
"\tvariant <id> {\n"
"\t\tstruct {\n"
"\t\t\tuint27_clock_monotonic_t timestamp;\n"
"\t\t} compact;\n"
"\t\tstruct {\n"
"\t\t\tuint32_t id;\n"
"\t\t\tuint64_clock_monotonic_t timestamp;\n"
"\t\t} extended;\n"
"\t} v;\n"
"} align(8);\n"
"\n"
"stream {\n"
"\tid = 0;\n"
"\tevent.header := struct event_header_compact;\n"
"\tpacket.context := struct packet_context;\n"
"};\n"
"\n"
"event {\n"
"\tname = \"sched_switch\";\n"
"\tid = 0;\n"
"\tstream_id = 0;\n"
"\tfields := struct {\n"
"\t\tinteger { size = 8; align = 8; signed = 0; encoding = UTF8; base = 10;}\
 _prev_comm[16];\n"
"\t\tinteger { size = 32; align = 8; signed = 1; encoding = none; base = 10; }\
 _prev_tid;\n"
"\t\tinteger { size = 32; align = 8; signed = 1; encoding = none; base = 10; }\
 _prev_prio;\n"
"\t\tinteger { size = 64; align = 8; signed = 1; encoding = none; base = 10; }\
 _prev_state;\n"
"\t\tinteger { size = 8; align = 8; signed = 0; encoding = UTF8; base = 10; }\
 _next_comm[16];\n"
"\t\tinteger { size = 32; align = 8; signed = 1; encoding = none; base = 10; }\
 _next_tid;\n"
"\t\tinteger { size = 32; align = 8; signed = 1; encoding = none; base = 10; }\
 _next_prio;\n"
"\t};\n"
"};"
;

void generate_metadata()
{
  FILE *file = fopen( "metadata", "w" );
  assert( file != NULL );
  fwrite( metadata, sizeof( metadata ) - 1, 1, file );
  fclose( file );
}

int main( int argc, char **argv )
{
  rtems_record_client_context  ctx;
  client_context               cctx;
  packet_context               pkt_ctx;
  size_t                       pkt_ctx_size;
  const char                  *host;
  uint16_t                     port;
  const char                  *input_file;
  bool                         input_file_flag;
  bool                         input_TCP_host;
  bool                         input_TCP_port;
  int                          fd;
  int                          rv;
  int                          opt;
  int                          longindex;
  size_t                       i;
  char                         filename[ 256 ];

  host = "127.0.0.1";
  port = 1234;
  input_file = "raw_data";
  input_file_flag = false;
  input_TCP_host = false;
  input_TCP_port = false;

  while (
    ( opt = getopt_long( argc, argv, "hH:p:i:", &longopts[0], &longindex ) )
      != -1
  ) {
    switch ( opt ) {
      case 'h':
        usage( argv );
        exit( EXIT_SUCCESS );
        break;
      case 'H':
        host = optarg;
        input_TCP_host = true;
        break;
      case 'p':
        port = (uint16_t) strtoul( optarg, NULL, 10 );
        input_TCP_port = true;
        break;
      case 'i':
        input_file = optarg;
        assert( input_file != NULL );
        input_file_flag = true;
        break;
      default:
        exit( EXIT_FAILURE );
        break;
    }
  }

  if( input_file_flag && ( input_TCP_host || input_TCP_port ) ) {
    printf( "There should be one input medium\n" );
    exit( EXIT_SUCCESS );
  }

  memset( &cctx, 0, sizeof( cctx ) );

  generate_metadata();

  memset( &pkt_ctx, 0, sizeof( pkt_ctx ) );
  memcpy( pkt_ctx.header.uuid, uuid, sizeof( pkt_ctx.header.uuid ) );
  pkt_ctx.header.ctf_magic = CTF_MAGIC;

  for ( i = 0; i < RTEMS_RECORD_CLIENT_MAXIMUM_CPU_COUNT; ++i ) {
    FILE *f;

    snprintf( filename, sizeof( filename ), "event_%zu", i );
    f = fopen( filename, "wb" );
    assert( f != NULL );
    cctx.per_cpu[ i ].event_stream = f;
    fwrite( &pkt_ctx, sizeof( pkt_ctx ), 1, f );
  }

  fd = connect_client( host, port, input_file, input_file_flag );
  rtems_record_client_init( &ctx, handler, &cctx );

  while ( true ) {
    int buf[ 8192 ];
    ssize_t n;

    n = ( input_file_flag ) ? read( fd, buf, sizeof( buf ) ) :
    recv( fd, buf, sizeof( buf ), 0 );
    if ( n > 0 ) {
      rtems_record_client_run( &ctx, buf, (size_t) n );
    } else {
      break;
    }
  }

  rtems_record_client_destroy( &ctx );
  pkt_ctx_size = sizeof( pkt_ctx ) * BITS_PER_CHAR;

  for ( i = 0; i < RTEMS_RECORD_CLIENT_MAXIMUM_CPU_COUNT; ++i ) {
    per_cpu_context *pcpu;

    pcpu = &cctx.per_cpu[ i ];
    fseek( pcpu->event_stream, 0, SEEK_SET );

    pkt_ctx.header.stream_instance_id = i;
    pkt_ctx.timestamp_begin = pcpu->timestamp_begin;
    pkt_ctx.timestamp_end = pcpu->timestamp_end;
    pkt_ctx.content_size = pcpu->content_size + pkt_ctx_size;
    pkt_ctx.packet_size = pcpu->packet_size + pkt_ctx_size;
    pkt_ctx.cpu_id = i;

    fwrite( &pkt_ctx, sizeof( pkt_ctx ), 1, pcpu->event_stream );
    fclose( pcpu->event_stream );
  }

  rv = close( fd );
  assert( rv == 0 );

  return 0;
}
