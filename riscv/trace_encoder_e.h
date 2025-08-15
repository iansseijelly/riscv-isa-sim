#ifndef _RISCV_TRACE_ENCODER_E_H
#define _RISCV_TRACE_ENCODER_E_H

#include <cstdint>
#include <stdio.h>

#include <cassert>
#include <string>
#include <variant>
#include <vector>

#include "abstract_trace_encoder.h"
#include "bp_double_saturating_counter.h"
#include "branch_predictor.h"
#include "common.h"

class processor_t;

enum fmt_t {
    FMT_3 = 0b11,
    FMT_2 = 0b10,
    FMT_1 = 0b01,
};

enum subfmt_t {
    SUBFMT_START = 0b00,
    SUBFMT_TRAP = 0b01,
    SUBFMT_CONTEXT = 0b10,
    SUBFMT_SUPPORT = 0b11
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
    bool updiscon;
};

struct flags_t {
    bool notify;
    bool prev_updiscon;
    bool curr_updiscon;
    bool updiscon;
    bool irreport;
    bool irdepth;
    bool prev_exception;
    bool curr_exception;
    bool curr_exc_only;
    bool next_exc_only;
    bool next_exception;
    bool prev_reported;
    bool first_qualified;
    bool ppccd;
    bool ppccd_br;
    bool er_n;
    bool resync_br;
    bool rpt_br;
    bool resync_exceeded;
    // bool 
};

enum trace_encoder_e_state_t {
    TRACE_ENCODER_E_IDLE,
    TRACE_ENCODER_E_DATA,
};

using trace_encoder_e_packet_t = std::variant<sync_packet_t, branch_map_packet_t>;

#define MAX_TRACE_BUFFER_SIZE 256 // arbitrary
#define MAX_COMPRESS_DELTA 6
#define RESYNC_MAX 32 // arbitrary
#define MAX_BRANCHES 31

class trace_encoder_e : public abstract_trace_encoder_t {
   public:
    trace_encoder_e() {
        this->active = true;
        this->enabled = false;
        this->next_ingress = hart_to_encoder_ingress_t();
        this->curr_ingress = hart_to_encoder_ingress_t();
        this->prev_ingress = hart_to_encoder_ingress_t();
        this->state = TRACE_ENCODER_E_IDLE;
        this->br_mode = BR_TARG;
        this->branches = 0;
        this->resync_count = 0;
        this->trap_reported = false;
        this->branch_map.resize(31);
        this->buffer.resize(256);
        this->buffer.clear();
    }
    void reset() override;
    void set_enable(bool enabled) override;
    bool get_enable() override;
    void set_br_mode(br_mode_t br_mode) override;
    void init_trace_file() override;
    void push_ingress(hart_to_encoder_ingress_t packet) override;

   private:
    void _bt_mode_data_step();
    void _init_flags(hart_to_encoder_ingress_t *iprev, hart_to_encoder_ingress_t *icurr, hart_to_encoder_ingress_t *inext);
    void _update_branch_map(bool taken);
    void _generate_sync_packet(subfmt_t subfmt, hart_to_encoder_ingress_t *icurr, bool thaddr, hart_to_encoder_ingress_t *iexception);
    void _generate_branch_packet(hart_to_encoder_ingress_t *icurr, bool with_address, hart_to_encoder_ingress_t *iprev);
    void _encode_sync_packet();
    void _encode_branch_packet();
    uint32_t _convert_branch_map();
    void _log_packet(trace_encoder_e_packet_t* packet);
    void load_buffer(std::vector<uint8_t> buffer, std::string data);
    void set_status_fields(branch_map_packet_t *packet, bool msb);
    bool get_msb(uint64_t value);

    std::vector<uint8_t> buffer;
    uint8_t num_bytes;
    uint8_t bits_uncompressed_diff;
    trace_encoder_e_packet_t packet;
    // trace files
    FILE* trace_sink;
    FILE* trace_log;
    FILE* debug_reference;
    // ingress packets
    hart_to_encoder_ingress_t next_ingress;
    hart_to_encoder_ingress_t curr_ingress;
    hart_to_encoder_ingress_t prev_ingress;
    // encoder states
    bool active;
    bool enabled;
    trace_encoder_e_state_t state;
    br_mode_t br_mode;
    int branches;
    int resync_count;
    bool trap_reported;
    std::vector<bool> branch_map;
    flags_t flags;
    // previous values
    uint64_t prev_timestamp;
};

#endif
