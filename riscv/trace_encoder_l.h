#ifndef _RISCV_TRACE_ENCODER_L_H
#define _RISCV_TRACE_ENCODER_L_H

#include "trace_ingress.h"
#include "common.h"
#include <stdio.h>

class processor_t;

enum c_header_t {
	C_TB = 0b00, // taken branch
	C_NT = 0b01, // not taken branch
	C_NA = 0b10, // not applicable
	C_IJ = 0b11, // inferable jump
};

enum f_header_t {
	F_TB   = 0b000, // taken branch
	F_NT   = 0b001, // non taken branch
	F_UJ   = 0b010, // uninferable jump
	F_IJ   = 0b011, // inferable jump
	F_TRAP = 0b100, // trapping happened - could be interrupt or exception
	F_SYNC = 0b101, // a synchronization packet
	F_VAL  = 0b110, // this packets report a certain value upon request
	F_RES  = 0b111, // reserved for now
};

enum trap_type_t {
  T_NONE        = 0b000,
  T_EXCEPTION   = 0b001,
  T_INTERRUPT   = 0b010,
  T_TRAP_RETURN = 0b100,
};

struct trace_encoder_l_packet_t {
  c_header_t c_header;
  f_header_t f_header;
  trap_type_t trap_type;
  uint64_t address;
  uint64_t timestamp;
};

enum trace_encoder_l_state_t {
  TRACE_ENCODER_L_IDLE,
  TRACE_ENCODER_L_DATA,
};

#define MAX_TRACE_BUFFER_SIZE 32
#define MAX_COMPRESS_DELTA 6

void print_packet(trace_encoder_l_packet_t* packet);
void print_encoded_packet(uint8_t* buffer, int num_bytes);

int find_msb(uint64_t x);
int ceil_div(int a, int b);
int encode_varlen(uint64_t value, uint8_t* buffer);
c_header_t get_c_header(f_header_t f_header);

class trace_encoder_l {
public:
  trace_encoder_l() {
    this->trace_sink = fopen("trace_l.bin", "wb");
    this->debug_reference = fopen("trace_l_ref_debug.log", "wb");
    this->active = true;
    this->enabled = false;
    this->ingress_0 = hart_to_encoder_ingress_t();
    this->ingress_1 = hart_to_encoder_ingress_t();
    this->state = TRACE_ENCODER_L_IDLE;
  }
  void reset();
  void set_enable(bool enabled);
  bool get_enable();
  
  void push_ingress(hart_to_encoder_ingress_t packet);
  
private:
  void _generate_sync_packet();
  void _generate_direct_packet(f_header_t f_header);
  void _generate_jump_uninferable_packet();
  void _generate_trap_packet(trap_type_t trap_type);
  int _encode_compressed_packet(trace_encoder_l_packet_t* packet, uint8_t* buffer);
  int _encode_non_compressed_header(trace_encoder_l_packet_t* packet, uint8_t* buffer);
  int _encode_varlen(uint64_t value, uint8_t* buffer);
  
  uint8_t buffer[MAX_TRACE_BUFFER_SIZE];
  trace_encoder_l_packet_t packet;
  // trace files
  FILE* trace_sink;
  FILE* debug_reference;
  // ingress packets
  hart_to_encoder_ingress_t ingress_0;
  hart_to_encoder_ingress_t ingress_1;
  // encoder states
  bool active;
  bool enabled;
  trace_encoder_l_state_t state;
  // previous values
  uint64_t prev_timestamp;
};

#endif