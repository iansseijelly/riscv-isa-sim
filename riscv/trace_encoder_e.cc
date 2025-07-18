#include "trace_encoder_e.h"

#include <bitset>
#include <iostream>
#include <string>
#include <algorithm>
#include <variant>

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
            _generate_sync_packet(SUBFMT_START, 0);
            this->state = TRACE_ENCODER_E_DATA;
        } else if (this->state == TRACE_ENCODER_E_DATA) {
            _bt_mode_data_step();
        }
    } else if (!this->enabled) {
        if (this->state == TRACE_ENCODER_E_DATA) {
            _generate_sync_packet(SUBFMT_START, 0);
            this->state = TRACE_ENCODER_E_IDLE;
        }
    }
}

void trace_encoder_e::_bt_mode_data_step() {
    if (this->ingress_0.i_type == I_BRANCH_NON_TAKEN || this->ingress_0.i_type == I_BRANCH_TAKEN) {
        _update_branch_map(this->ingress_0.i_type == I_BRANCH_TAKEN);
    }
    switch (this->ingress_1.i_type) {
        case I_BRANCH_TAKEN:
            _generate_branch_packet(1);
            break;
        case I_BRANCH_NON_TAKEN:
            _generate_branch_packet(1);
            break;
        case I_JUMP_INFERABLE:
            _generate_branch_packet(0);
            break;
        case I_JUMP_UNINFERABLE:
            _generate_branch_packet(1);
            break;
        case I_EXCEPTION:
            _generate_sync_packet(SUBFMT_TRAP, 1);
            break;
        case I_INTERRUPT:
            _generate_sync_packet(SUBFMT_TRAP, 1);
            break;
        case I_TRAP_RETURN:
            _generate_sync_packet(SUBFMT_TRAP, 0);
            break;
    }
}

void trace_encoder_e::_update_branch_map(bool taken) {
    if (this->branches == 31) {
        this->branches = 0;
    }
    this->branches += 1;
    this->branch_map[this->branches - 1] = taken;
}

void trace_encoder_e::_generate_sync_packet(subfmt_t subfmt, bool thaddr) {
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

        this->num_bytes = _encode_sync_packet();
        // write the packet to the trace sink and log it
        if (this->trace_sink && this->num_bytes > 0) {
            fwrite(this->buffer.data(), sizeof(uint8_t), num_bytes, this->trace_sink);
            _log_packet(&this->packet);
        }
        
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

        this->num_bytes = _encode_branch_packet();
        // write the packet to the trace sink
        if (this->trace_sink && this->num_bytes > 0) {
            fwrite(this->buffer.data(), sizeof(uint8_t), num_bytes, this->trace_sink);
            _log_packet(&this->packet);
        }
        
    }
}

uint8_t trace_encoder_e::_encode_sync_packet() {
    this->buffer.clear();
    auto* a = std::get_if<sync_packet_t>(&this->packet);
    uint8_t num_bytes = 0;
    switch (a->subfmt) {
        case SUBFMT_START: {
            std::string packet = "";

            std::string fmt = std::bitset<2>(a->fmt).to_string();
            std::string subfmt = std::bitset<2>(a->subfmt).to_string();
            std::string branch = std::to_string(a->branch);
            std::string privilege = std::bitset<3>(a->privilege).to_string();
            std::string time = std::bitset<64>(a->time).to_string();
            std::string address = std::bitset<63>(a->address).to_string();

            std::reverse(fmt.begin(), fmt.end());
            std::reverse(subfmt.begin(), subfmt.end());
            std::reverse(privilege.begin(), privilege.end());
            std::reverse(time.begin(), time.end());
            std::reverse(address.begin(), address.end());


            packet.append(fmt);
            packet.append(subfmt);
            packet.append(branch);
            packet.append(privilege);
            packet.append(time);
            packet.append(address);

            num_bytes = load_buffer(this->buffer, packet);
            break;
        }
        case SUBFMT_TRAP: {
            std::string packet = "";
            std::string fmt = std::bitset<2>(a->fmt).to_string();
            std::string subfmt = std::bitset<2>(a->subfmt).to_string();
            std::string branch = std::to_string(a->branch);
            std::string privilege = std::bitset<3>(a->privilege).to_string();
            std::string time = std::bitset<64>(a->time).to_string();
            std::string ecause = std::bitset<4>(a->ecause).to_string();
            std::string interrupt = std::to_string(a->interrupt);
            std::string thaddr = std::to_string(a->thaddr);
            std::string address = std::bitset<63>(a->address).to_string();
            std::string tval = std::bitset<64>(a->tval).to_string();

            std::reverse(fmt.begin(), fmt.end());
            std::reverse(subfmt.begin(), subfmt.end());
            std::reverse(privilege.begin(), privilege.end());
            std::reverse(time.begin(), time.end());
            std::reverse(ecause.begin(), ecause.end());
            std::reverse(address.begin(), address.end());
            std::reverse(tval.begin(), tval.end());


            packet.append(fmt);
            packet.append(subfmt);
            packet.append(branch);
            packet.append(privilege);
            packet.append(time);
            packet.append(ecause);
            packet.append(interrupt);
            packet.append(thaddr);
            packet.append(address);
            packet.append(tval);

            num_bytes = load_buffer(this->buffer, packet);
            break;
        }
        default:
            break;
    }
    return num_bytes;
}

uint8_t trace_encoder_e::_encode_branch_packet() {
    this->buffer.clear();
    auto* a = std::get_if<branch_map_packet_t>(&this->packet);
    uint8_t num_bytes = 0;

    switch (a->fmt) {
        case FMT_1: {

            // convert to bitstrings
            std::string packet = "";
            std::string fmt = std::bitset<2>(a->fmt).to_string();
            std::string branches = std::bitset<2>(a->branches).to_string();
            size_t branch_map_length;
            if (a->branches <= 4) {
                branch_map_length = 3;
            } else if (a->branches <= 7) {
                branch_map_length = 7;
            } else if (a->branches <= 15) {
                branch_map_length = 15;
            } else if (a->branches <= 31) {
                branch_map_length = 31;
            }
            std::string branch_map = std::bitset<31>(a->branch_map).to_string().substr(0, branch_map_length);
            std::string address = std::bitset<63>(a->address).to_string();
            std::string notify = std::to_string(a->notify);

            // reverse for easier
            std::reverse(fmt.begin(), fmt.end());
            std::reverse(branches.begin(), branches.end());
            std::reverse(branch_map.begin(), branch_map.end());
            std::reverse(address.begin(), address.end());
            

            packet.append(fmt);
            packet.append(branches);
            packet.append(branch_map);
            packet.append(address);
            packet.append(notify);


            num_bytes = load_buffer(this->buffer, packet);
            break;
        }
        case FMT_2: {
            std::string packet = "";

            std::string fmt = std::bitset<2>(a->fmt).to_string();
            std::string address = std::bitset<63>(a->address).to_string();

            std::reverse(fmt.begin(), fmt.end());
            std::reverse(address.begin(), address.end());

            packet.append(fmt);
            packet.append(address);

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
    FILE* trace_log = this->trace_log;

    std::visit([&](auto&& arg) {
        // You can now use local_var here
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, sync_packet_t>) {
            auto pkt = static_cast<sync_packet_t>(arg);
            fprintf(this->trace_log,
                    "[Packet]: fmt: %d, subfmt: %d, branch: %d, privilege: %d, time: %d, ecause: %d, interrupt: %d, thaddr: %x, address: %x, tval: %d\n",
                    pkt.fmt, pkt.subfmt, pkt.branch, pkt.privilege, pkt.time, pkt.ecause, pkt.interrupt, pkt.thaddr, pkt.address << 1, pkt.tval);
        } else if constexpr (std::is_same_v<T, branch_map_packet_t>) {
            auto pkt = static_cast<branch_map_packet_t>(arg);
            switch (pkt.fmt) {
                case FMT_1: {
                    fprintf(this->trace_log,
                            "[Packet]: fmt: %d, branches: %d, branch_map: %d, address: %x, notify: %d\n",
                            pkt.fmt, pkt.branches, pkt.branch_map, pkt.address << 1, pkt.notify);
                    break;
                }

                case FMT_2: {
                    fprintf(this->trace_log,
                            "[Packet]: fmt: %d, address: %x, notify: %d\n",
                            pkt.fmt, pkt.address << 1, pkt.notify);
                    break;
                }

                default:
                    break;
            }
        }
    },
               *packet);
}

uint8_t trace_encoder_e::load_buffer(std::vector<uint8_t> buffer, std::string data) {
    uint8_t num_bytes = 0;
    // compress the packet
    int cutoff = data.size();
    for (int i = data.size() - 1; i > 0; i--) {
        if (data[i] != data[i - 1]) {
            cutoff = i + 1;
            break;
        }
    }
    data = data.substr(0, cutoff);

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
        std::string byte_string = data.substr(i * 8, 8);
        std::reverse(byte_string.begin(), byte_string.end());
        uint8_t byte = std::stoi(byte_string, nullptr, 2);
        this->buffer.push_back(byte);
    }


    return num_bytes;
}