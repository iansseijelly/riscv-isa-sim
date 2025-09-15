#ifndef _RISCV_ABSTRACT_TRACE_ENCODER__H
#define _RISCV_ABSTRACT_TRACE_ENCODER__H

#include "trace_ingress.h"

enum br_mode_t {
	BR_TARG = 0b00, // branch target mode
	BR_HIST = 0b01, // branch history mode
	BR_PRED = 0b10, // branch prediction mode
	BR_RSVD = 0b11, // reserved
};

enum ctx_mode_t {
  CTX_NONE  = 0b00, // ignore context
  CTX_WATCH = 0b01, // watch a specific context
  CTX_ALL   = 0b10, // trace across contexts
  CTX_RSVD  = 0b11, // reserved
};

class abstract_trace_encoder_t {
public:
    virtual ~abstract_trace_encoder_t() = default;

    virtual bool get_enable() { return false; };
    virtual void set_enable(bool enabled) { } ;
    virtual void set_br_mode(br_mode_t br_mode) { };
    virtual void set_ctx_mode(ctx_mode_t ctx_mode) { };
    virtual void set_ctx_id(uint32_t ctx_id) { };
    virtual void init_trace_file() { };
    virtual void reset() { };
    virtual void push_ingress(hart_to_encoder_ingress_t packet) { };
};

#endif  // _RISCV_ABSTRACT_TRACE_ENCODER__H
