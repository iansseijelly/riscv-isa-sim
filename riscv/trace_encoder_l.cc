#include "trace_encoder_l.h"

void trace_encoder_l::reset() {
  this->state = TRACE_ENCODER_L_IDLE;
  this->active = true;
  this->enabled = false;
  this->ingress_0 = hart_to_encoder_ingress_t(); // create empty packet
  this->ingress_1 = hart_to_encoder_ingress_t(); // create empty packet
}

void trace_encoder_l::set_enable(bool enabled) {
  this->enabled = enabled;
}

void trace_encoder_l::push_ingress(hart_to_encoder_ingress_t packet) {
  this->ingress_1 = this->ingress_0;
  this->ingress_0 = packet;
  if (this->enabled) {
    fprintf(this->debug_reference, "%lx\n", packet.i_addr);
    if (this->state == TRACE_ENCODER_L_IDLE) {
      _generate_sync_packet();
      this->state = TRACE_ENCODER_L_DATA;
    } else if (this->state == TRACE_ENCODER_L_DATA) {
      if (this->ingress_1.i_type == I_BRANCH_TAKEN) {
        _generate_branch_taken_packet();
      } else if (this->ingress_1.i_type == I_BRANCH_NON_TAKEN) {
        _generate_branch_non_taken_packet();
      } else if (this->ingress_1.i_type == I_JUMP_INFERABLE) {
        _generate_jump_inferable_packet();
      } else if (this->ingress_1.i_type == I_JUMP_UNINFERABLE) {
        _generate_jump_uninferable_packet();
      }
    }
  }
}

void trace_encoder_l::_generate_sync_packet() {
  // set packet fields
  this->packet.c_header = C_NA;
  this->packet.f_header = F_SYNC;
  this->packet.address = this->ingress_0.i_addr;
  this->packet.timestamp = this->ingress_0.i_timestamp;
  // encode the packet
  int num_bytes = 0;
  num_bytes += encode_non_compressed_header(&this->packet, this->buffer);
  num_bytes += encode_varlen(this->packet.address, this->buffer + num_bytes);
  num_bytes += encode_varlen(this->packet.timestamp, this->buffer + num_bytes);
  // write the packet to the trace sink
  fwrite(this->buffer, 1, num_bytes, this->trace_sink);
}

void trace_encoder_l::_generate_direct_packet(f_header_t f_header) {
  int delta_timestamp = this->ingress_1.i_timestamp - this->prev_timestamp;
  this->prev_timestamp = this->ingress_1.i_timestamp;
  int msb = find_msb(delta_timestamp);
  bool is_compressed = msb < MAX_COMPRESS_DELTA;
  this->packet.timestamp = delta_timestamp;
  this->packet.c_header = is_compressed ? get_c_header(f_header) : C_NA;
  this->packet.f_header = f_header;
  int num_bytes = 0;
  if (is_compressed) { 
    num_bytes += encode_compressed_packet(&this->packet, this->buffer); 
  } else {
    num_bytes += encode_non_compressed_header(&this->packet, this->buffer);
    num_bytes += encode_varlen(this->packet.timestamp, this->buffer + num_bytes);
  }
  fwrite(this->buffer, 1, num_bytes, this->trace_sink);
}

void trace_encoder_l::_generate_jump_uninferable_packet() {
  this->packet.c_header = C_NA;
  this->packet.f_header = F_UJ;
  // calculate the address
  uint64_t e_addr = this->ingress_0.i_addr >> 1;
  this->packet.address = e_addr ^ this->prev_addr;
  this->prev_addr = e_addr;
  // calculate the timestamp
  this->packet.timestamp = this->ingress_1.i_timestamp - this->prev_timestamp;
  this->prev_timestamp = this->ingress_1.i_timestamp;
  // encode the packet
  int num_bytes = 0;
  num_bytes += encode_non_compressed_header(&this->packet, this->buffer);
  num_bytes += encode_varlen(this->packet.address, this->buffer + num_bytes);
  num_bytes += encode_varlen(this->packet.timestamp, this->buffer + num_bytes);
  fwrite(this->buffer, 1, num_bytes, this->trace_sink);
}

// returns the 0-index of the most significant bit
int find_msb(uint64_t x) {
  if (x == 0) return -1;
  return 63 - __builtin_clzll(x);
}

// returns the ceiling of the division of a 0-indexed a by b
int ceil_div(int a, int b) {
  return a / b + 1;
}

int encode_compressed_packet(trace_encoder_l_packet_t* packet, uint8_t* buffer) {
  buffer[0] = packet->c_header | packet->timestamp << 2;
  return 1;
}

int encode_non_compressed_header(trace_encoder_l_packet_t* packet, uint8_t* buffer) {
  buffer[0] = packet->c_header | packet->f_header << 2;
  return 1;
}

// encodes a uint64_t into a buffer using a variable-length encoding
// returns the number of bytes used for encoding the address
int encode_varlen(uint64_t value, uint8_t* buffer) {
  int msb = find_msb(value);
  int num_bytes = ceil_div(msb, 7);
  for (int i = 0; i < num_bytes; i++) {
    buffer[i] = (value & 0x7F) | (i == num_bytes - 1 ? 0x80 : 0x00);
    value >>= 7;
  }
  return num_bytes;
}

c_header_t get_c_header(f_header_t f_header) {
  if (f_header == F_BRANCH_TAKEN) return C_TB;
  else if (f_header == F_BRANCH_NON_TAKEN) return C_NT;
  else if (f_header == F_JUMP_INFERABLE) return C_IJ;
  else return C_NA;
}