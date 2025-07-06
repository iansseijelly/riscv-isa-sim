#ifndef _RISCV_TRACE_ENCODER_E_H
#define _RISCV_TRACE_ENCODER_E_H

#include <stdio.h>

#include <cassert>
#include <variant>
#include <vector>
#include <string>

#include "bp_double_saturating_counter.h"
#include "branch_predictor.h"
#include "common.h"
#include "trace_ingress.h"

class processor_t;

enum br_mode_t {
    BR_TARG = 0b00,  // branch target mode
    BR_HIST = 0b01,  // branch history mode
    BR_PRED = 0b10,  // branch prediction mode
    BR_RSVD = 0b11,  // reserved
};

enum fmt_t {
    FMT_3 = 0b11,
    FMT_2 = 0b10,
    FMT_1 = 0b01,
};

enum subfmt_t {
    SUBFMT_START = 0b00,
    SUBFMT_TRAP = 0b01,
};

enum ecause_t {
    ECAUSE_NONE = 0b000,
    ECAUSE_EXCEPTION = 0b001,
    ECAUSE_INTERRUPT = 0b010,
    ECAUSE_TRAP_RETURN = 0b100,
};

struct sync_packet_t {
    fmt_t fmt;
    subfmt_t subfmt;
    bool branch;
    priv_enc privilege;
    uint64_t time;
    // context_t context;
    uint8_t ecause;
    bool interrupt;
    bool thaddr;

    uint64_t address;
    uint64_t tval;
};

struct branch_map_packet_t {
    fmt_t fmt;
    uint8_t branches : 5;
    uint32_t branch_map : 31;
    uint64_t address;
    bool notify;
    // bool updiscon;
    // bool irreport;
    // uint64_t irdepth;
};

enum trace_encoder_e_state_t {
    TRACE_ENCODER_E_IDLE,
    TRACE_ENCODER_E_DATA,
};

using trace_encoder_e_packet_t = std::variant<sync_packet_t, branch_map_packet_t>;

#define MAX_TRACE_BUFFER_SIZE 32
#define MAX_COMPRESS_DELTA 6

class trace_encoder_e {
   public:
    trace_encoder_e() {
        this->active = true;
        this->enabled = false;
        this->ingress_0 = hart_to_encoder_ingress_t();
        this->ingress_1 = hart_to_encoder_ingress_t();
        this->state = TRACE_ENCODER_E_IDLE;
        this->br_mode = BR_TARG;
        this->branches = 0;
        this->buffer.clear();
    }
    void reset();
    void set_enable(bool enabled);
    bool get_enable();
    void set_br_mode(br_mode_t br_mode);
    void init_trace_file();
    void push_ingress(hart_to_encoder_ingress_t packet);

   private:
    void _bt_mode_data_step();
    void _update_branch_map(bool taken);
    void _generate_sync_packet(subfmt_t subfmt, bool thaddr, uint8_t exception);
    void _generate_branch_packet(bool taken);
    uint8_t _encode_sync_packet(subfmt_t subfmt);
    uint8_t _encode_branch_packet();
    uint8_t _encode_varlen(uint64_t value, uint8_t num_bytes);
    uint32_t _convert_branch_map();
    void _log_packet(trace_encoder_e_packet_t* packet);
    uint8_t load_buffer(std::vector<uint8_t> buffer, std::string data);

    std::vector<uint8_t> buffer;
    uint8_t num_bytes;
    trace_encoder_e_packet_t packet;
    // trace files
    FILE* trace_sink;
    FILE* trace_log;
    FILE* debug_reference;
    // ingress packets
    hart_to_encoder_ingress_t ingress_0;
    hart_to_encoder_ingress_t ingress_1;
    // encoder states
    bool active;
    bool enabled;
    trace_encoder_e_state_t state;
    br_mode_t br_mode;
    uint8_t branches;
    std::vector<bool> branch_map;
    // previous values
    uint64_t prev_timestamp;
};

#endif
