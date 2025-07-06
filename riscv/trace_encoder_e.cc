#include "trace_encoder_e.h"

#include <bitset>
#include <string>

void trace_encoder_e::reset() {
    this->active = true;
    this->enabled = false;
    this->ingress_0 = hart_to_encoder_ingress_t();
    this->ingress_1 = hart_to_encoder_ingress_t();
}

void trace_encoder_e::set_enable(bool enabled) {
    this->enabled = enabled;
}

bool trace_encoder_e::get_enable() {
    return this->enabled;
}

void trace_encoder_e::set_br_mode(br_mode_t br_mode) {
    this->br_mode = br_mode;
}

void trace_encoder_e::init_trace_file() {
    this->trace_sink = fopen("etrace.out", "wb");
    this->trace_log = fopen("etrace.log", "wb");
    this->debug_reference = fopen("etrace.debug", "wb");
}

void trace_encoder_e::push_ingress(hart_to_encoder_ingress_t packet) {
    this->ingress_1 = this->ingress_0;
    this->ingress_0 = packet;
    if (this->enabled) {
        fprintf(this->debug_reference, "%lx, %d\n", packet.i_addr, packet.i_type);
        if (this->state == TRACE_ENCODER_E_IDLE) {
            _generate_sync_packet(SUBFMT_START, 0, ECAUSE_NONE);
            this->state = TRACE_ENCODER_E_DATA;
        } else if (this->state == TRACE_ENCODER_E_DATA) {
            if (this->br_mode == BR_TARG) {
                _bt_mode_data_step();
            }
        }
    } else if (!this->enabled) {
        if (this->state == TRACE_ENCODER_E_DATA) {
            _generate_sync_packet(SUBFMT_START, 0, ECAUSE_NONE);
            this->state = TRACE_ENCODER_E_IDLE;
        }
    }
}

void trace_encoder_e::_bt_mode_data_step() {
    if (this->ingress_0.i_type == I_BRANCH_NON_TAKEN || this->ingress_0.i_type == I_BRANCH_TAKEN) {
        _update_branch_map(this->ingress_0.i_type == I_BRANCH_TAKEN);
    }
    switch (this->ingress_0.i_type) {
        case I_BRANCH_TAKEN:
            // _generate_branch_packet(1);
            break;
        case I_BRANCH_NON_TAKEN:
            // _generate_branch_packet(0);
            break;
        case I_JUMP_INFERABLE:
            // _generate_branch_packet(1);
            break;
        case I_JUMP_UNINFERABLE:
            // _generate_trap_packet(ECAUSE_NONE, 0);
            break;
        case I_EXCEPTION:
            // _generate_trap_packet(T_EXCEPTION);
            break;
        case I_INTERRUPT:
            // _generate_trap_packet(T_INTERRUPT);
            break;
        case I_TRAP_RETURN:
            // _generate_branch_packet(1);
            break;
    }
}

void trace_encoder_e::_update_branch_map(bool taken) {
    this->branches += 1;
    this->branch_map[this->branches - 1] = taken;
}

void trace_encoder_e::_generate_sync_packet(subfmt_t subfmt, bool thaddr, uint8_t exception) {
    this->packet = sync_packet_t{};
    auto* a = std::get_if<sync_packet_t>(&this->packet);
    if (a != NULL) {
        a->fmt = FMT_3;
        a->subfmt = subfmt;
        if (subfmt == SUBFMT_START || SUBFMT_TRAP) {
            a->branch = this->ingress_0.i_type == I_BRANCH_TAKEN ? 0 : 1;
            a->privilege = this->ingress_0.priv;
            a->time = this->ingress_0.i_timestamp;
            // add context here later
        }
        if (subfmt == SUBFMT_START) {
            a->address = this->ingress_0.i_addr >> 1;
        } else if (subfmt == SUBFMT_TRAP) {
            a->ecause = this->ingress_0.exc_cause;
            a->interrupt = this->ingress_0.i_type == I_INTERRUPT ? 1 : 0;
            a->thaddr = thaddr;
            a->address = this->ingress_0.i_addr >> 1;

            if (a->interrupt == 0) {
                a->tval = this->ingress_0.tval;
            }
        } else {
            exit(EXIT_FAILURE);
        }

        // TODO - figure out encoding
        this->num_bytes = _encode_sync_packet(a->subfmt);
    }
}

void trace_encoder_e::_generate_branch_packet(bool with_address) {
    this->packet = branch_map_packet_t{};
    auto* a = std::get_if<branch_map_packet_t>(&this->packet);
    if (a != NULL) {
        a->branches = this->branches;
        if (this->branch_map.size() == 0) {
            a->branch_map = 0;
        } else {
            a->branch_map = _convert_branch_map();
        }
        if (with_address) {
            a->address = (this->ingress_0.i_addr - this->ingress_1.i_addr) >> 1;
            if (a->branches != 0) {
                a->fmt = FMT_1;
            } else {
                a->fmt = FMT_2;
                a->branches = 0;
                a->branch_map = 0;
            }
            // TODO - handle status fields (notify/updiscon/irreport/irdepth)
        } else {
            a->fmt = FMT_1;
            if (a->branches == 31) {
                a->branches = 0;
            }
        }

        // TODO - figure out encoding
    }
}

uint8_t trace_encoder_e::_encode_sync_packet(subfmt_t subfmt) {
    this->buffer.clear();
    auto* a = std::get_if<sync_packet_t>(&this->packet);
    uint8_t num_bytes = 0;
    switch (subfmt) {
        case SUBFMT_START: {
            std::string packet = "";
            packet.append(std::bitset<2>(a->fmt).to_string());
            packet.append(std::bitset<2>(a->subfmt).to_string());
            packet.append(std::to_string(a->branch));
            packet.append(std::bitset<3>(a->privilege).to_string());
            packet.append(std::bitset<64>(a->time).to_string());
            packet.append(std::bitset<63>(a->address).to_string());

            num_bytes = load_buffer(this->buffer, packet);
            break;
        }
        case SUBFMT_TRAP: {
            std::string packet = "";
            packet.append(std::bitset<2>(a->fmt).to_string());
            packet.append(std::bitset<2>(a->subfmt).to_string());
            packet.append(std::to_string(a->branch));
            packet.append(std::bitset<3>(a->privilege).to_string());
            packet.append(std::bitset<64>(a->time).to_string());
            packet.append(std::bitset<4>(a->ecause).to_string());
            packet.append(std::to_string(a->interrupt));
            packet.append(std::to_string(a->thaddr));
            packet.append(std::bitset<63>(a->address).to_string());
            packet.append(std::bitset<64>(a->tval).to_string());

            num_bytes = load_buffer(this->buffer, packet);
            break;
        }
        default:
            break;
    }
    return num_bytes;
}

uint32_t trace_encoder_e::_convert_branch_map() {
    uint32_t result = 0;
    for (size_t i = 0; i < this->branches; ++i) {
        if (branch_map[this->branches - 1 - i]) {
            result |= (1 << i);
        }
    }
    return result;
}

void trace_encoder_e::_log_packet(trace_encoder_e_packet_t* packet) {
}

uint8_t trace_encoder_e::load_buffer(std::vector<uint8_t> buffer, std::string data) {
    uint8_t num_bytes = 0;
    // compress the packet
    for (int i = data.size() - 1; i > 0; i++) {
        if (data[i] != data[i - 1]) {
            data = data.substr(0, i + 1);
        }
    }

    // sign extend to byte boundary
    int remainder = data.size() % 8;
    if (remainder != 0) {
        char msb = data[data.size() - 1];
        std::string padding = "";
        for (int i = 0; i < remainder; i++) {
            padding += msb;
        }
        data = data + padding;
    }

    num_bytes = data.size() >> 3;

    // write to buffer
    for (int i = 0; i < num_bytes; i++) {
        buffer.push_back(std::stoi(data.substr(i * 8, 8), nullptr, 2));
    }

    return num_bytes;
}