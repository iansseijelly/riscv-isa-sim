#include "trace_encoder_e.h"
#include "trace_ingress.h"

#include <algorithm>
#include <bitset>
#include <iostream>
#include <string>
#include <variant>

void trace_encoder_e::reset() {
  this->active = true;
  this->enabled = false;
  this->curr_ingress = hart_to_encoder_ingress_t();
  this->prev_ingress = hart_to_encoder_ingress_t();
}

void trace_encoder_e::set_enable(bool enabled) { this->enabled = enabled; }

bool trace_encoder_e::get_enable() { return this->enabled; }

void trace_encoder_e::set_br_mode(br_mode_t br_mode) {
  this->br_mode = br_mode;
}

void trace_encoder_e::init_trace_file() {
  this->trace_sink = fopen("etrace.out", "wb");
  this->trace_log = fopen("etrace.log", "wb");
  this->debug_reference = fopen("etrace.debug", "wb");
}

void trace_encoder_e::push_ingress(hart_to_encoder_ingress_t packet) {
  this->prev_ingress = this->curr_ingress;
  this->curr_ingress = this->next_ingress;
  this->next_ingress = packet;
  if (this->enabled) {
    fprintf(this->debug_reference, "%lx, %d\n", curr_ingress.i_addr,
            curr_ingress.i_type);
    if (this->state == TRACE_ENCODER_E_IDLE) {
      _generate_sync_packet(SUBFMT_START, &this->curr_ingress, 0, NULL);
      this->state = TRACE_ENCODER_E_DATA;
    } else if (this->state == TRACE_ENCODER_E_DATA) {
      _bt_mode_data_step();
    }
  } else if (!this->enabled) {
    if (this->state == TRACE_ENCODER_E_DATA) {
      _generate_sync_packet(SUBFMT_START, &this->curr_ingress, 0, NULL);
      this->state = TRACE_ENCODER_E_IDLE;
    }
  }
}

void trace_encoder_e::_init_flags(hart_to_encoder_ingress_t *iprev,
                                  hart_to_encoder_ingress_t *icurr,
                                  hart_to_encoder_ingress_t *inext) {
  this->flags.notify = false;

  this->flags.prev_updiscon = (iprev->i_type == I_JUMP_UNINFERABLE) ||
                              (iprev->i_type == I_CALL_UNINFERABLE) ||
                              (iprev->i_type == I_OTHER_UNINFERABLE);
  this->flags.curr_updiscon = (icurr->i_type == I_JUMP_UNINFERABLE) ||
                              (icurr->i_type == I_CALL_UNINFERABLE) ||
                              (icurr->i_type == I_OTHER_UNINFERABLE);
  this->flags.updiscon =
      this->flags.updiscon &&
      ((inext->i_type == I_EXCEPTION) || (inext->i_type == I_INTERRUPT) ||
       (inext->priv !=
        icurr->priv) /* || (this->resync_count == this->resync_max) */);

  this->flags.irreport = false;
  this->flags.irdepth = 0;

  this->flags.prev_exception =
      (iprev->i_type == I_EXCEPTION) || (iprev->i_type == I_INTERRUPT);

  this->flags.curr_exc_only = (icurr->iretire == 0) &&
                              (icurr->i_type == I_EXCEPTION) &&
                              (icurr->i_type == I_INTERRUPT);
  this->flags.curr_exception =
      (icurr->i_type == I_EXCEPTION) && (icurr->i_type == I_INTERRUPT);

  this->flags.next_exc_only = (inext->iretire == 0) &&
                              (inext->i_type == I_EXCEPTION) &&
                              (inext->i_type == I_INTERRUPT);
  this->flags.next_exception =
      (inext->i_type == I_EXCEPTION) && (inext->i_type == I_INTERRUPT);

  this->flags.prev_reported = this->flags.prev_exception && this->trap_reported;
  this->trap_reported = false;

  this->flags.ppccd = (icurr->priv != iprev->priv);
  this->flags.ppccd_br =
      (icurr->priv != iprev->priv) /* || (icurr->context != iprev->context) */;

  this->flags.er_n = (((icurr->iretire > 0) and this->flags.curr_exception) ||
                      this->flags.notify);

  this->flags.resync_br =
      (this->resync_count == RESYNC_MAX) && (this->branches != 0);
  this->flags.rpt_br = (this->branches == MAX_BRANCHES) /* && (
          self.pbc < MAX_BRANCHES
      ) */
      ;
  this->flags.resync_exceeded = this->resync_count > RESYNC_MAX;
}

void trace_encoder_e::_bt_mode_data_step() {
  if (this->prev_ingress.i_type == I_BRANCH_TAKEN ||
      this->prev_ingress.i_type == I_BRANCH_NON_TAKEN) {
    this->_update_branch_map(this->prev_ingress.i_type == I_BRANCH_TAKEN);
    std::cout << "\ttaken branch at pc = " << std::hex << this->prev_ingress.i_addr << std::endl;
  }

  _init_flags(&this->prev_ingress, &this->curr_ingress, &this->next_ingress);

  if (this->flags.prev_exception) {
    if (this->flags.curr_exc_only) {
      this->_generate_sync_packet(SUBFMT_TRAP, &this->curr_ingress, 1,
                                  &this->prev_ingress);
    } else {
      if (this->flags.prev_reported) {
        this->_generate_sync_packet(SUBFMT_START, &this->curr_ingress, 0, NULL);
      } else {
        this->_generate_sync_packet(SUBFMT_TRAP, &this->curr_ingress, 1,
                                    &this->prev_ingress);
      }
    }
  } else if (this->flags.prev_updiscon) {
    if (this->flags.curr_exc_only) {
      this->_generate_sync_packet(SUBFMT_TRAP, &this->curr_ingress, 0,
                                  &this->curr_ingress);
    } else {
      this->_generate_branch_packet(&this->curr_ingress, 1,
                                    &this->prev_ingress);
    }
  } else if (this->flags.resync_br || this->flags.er_n) {
    this->_generate_branch_packet(&this->curr_ingress, 1, &this->prev_ingress);
  } else if (this->flags.next_exc_only || this->flags.ppccd_br) {
    this->_generate_branch_packet(&this->curr_ingress, 1, &this->prev_ingress);
  } else if (this->flags.rpt_br) {
    this->_generate_branch_packet(&this->curr_ingress, 0, &this->prev_ingress);
  }
}

void trace_encoder_e::_update_branch_map(bool taken) {
  this->branches += 1;
  this->branch_map[this->branches - 1] = taken;
}

void trace_encoder_e::_generate_sync_packet(
    subfmt_t subfmt, hart_to_encoder_ingress_t *icurr, bool thaddr,
    hart_to_encoder_ingress_t *iexception) {
  this->packet = sync_packet_t{};
  auto *a = std::get_if<sync_packet_t>(&this->packet);
  if (a != NULL) {
    a->fmt = FMT_3;
    a->subfmt = subfmt;
    if (subfmt == SUBFMT_START || SUBFMT_TRAP) {
      a->branch = icurr->i_type == I_BRANCH_TAKEN ? 0 : 1;
      a->privilege = icurr->priv;
      a->time = icurr->i_timestamp;
    }
    if (subfmt == SUBFMT_START) {
      a->address = icurr->i_addr >> 1;
    } else if (subfmt == SUBFMT_TRAP) {
      this->trap_reported = ((thaddr == 0) and (this->flags.prev_updiscon ||
                                                this->flags.next_exception));
      a->ecause = iexception->exc_cause;
      a->interrupt = iexception->i_type == I_INTERRUPT ? 1 : 0;
      a->thaddr = thaddr;
      a->address = icurr->i_addr >> 1;

      if (a->interrupt == 0) {
        a->tval = iexception->tval;
      }

    } else if (subfmt == SUBFMT_CONTEXT) {
      a->privilege = icurr->priv;
      a->time = icurr->i_timestamp;
    } else if (subfmt == SUBFMT_SUPPORT) {
      exit(EXIT_FAILURE);
    } else {
      exit(EXIT_FAILURE);
    }

    this->branches = 0; // reset branches

    _encode_sync_packet();
    // write the packet to the trace sink and log it
    if (this->trace_sink && this->num_bytes > 0) {
      // write the number of bytes before the packet
      fwrite(&this->num_bytes, sizeof(uint8_t), 1, this->trace_sink);
      fwrite(&this->num_bits_uncompressed, sizeof(uint16_t), 1,
             this->trace_sink);
      fwrite(this->buffer.data(), sizeof(uint8_t), this->num_bytes,
             this->trace_sink);
      _log_packet(&this->packet);
    }
  }
}

void trace_encoder_e::_generate_branch_packet(
    hart_to_encoder_ingress_t *icurr, bool with_address,
    hart_to_encoder_ingress_t *iprev) {
  this->packet = branch_map_packet_t{};
  auto *a = std::get_if<branch_map_packet_t>(&this->packet);
  if (a != NULL) {
    a->branches = this->branches;
    
    if (this->branch_map.size() == 0) {
      a->branch_map = 0;
    } else {
      a->branch_map = _convert_branch_map();
    }

    if (!with_address) {
      a->fmt = FMT_1;
      a->address = 0;
    } else {
      std::cout << "previous address: " << std::hex << iprev->i_addr << ", current address: " << icurr->i_addr << std::endl;
      a->address = (icurr->i_addr - iprev->i_addr) >> 1;
      if (a->branches != 0) {
        a->fmt = FMT_1;
      } else {
        a->fmt = FMT_2;
      }
      a->notify = this->flags.notify;
      a->updiscon = this->flags.updiscon ^ this->flags.notify;
    }

    if (a->branches == 31) {
      a->branches = 0;
    }

    this->branches = 0; // reset branches

    _encode_branch_packet();
    // write the packet to the trace sink
    if (this->trace_sink && this->num_bytes > 0) {
      // write the number of bytes
      fwrite(&this->num_bytes, sizeof(uint8_t), 1, this->trace_sink);
      fwrite(&this->num_bits_uncompressed, sizeof(uint16_t), 1,
             this->trace_sink);
      fwrite(this->buffer.data(), sizeof(uint8_t), num_bytes, this->trace_sink);
      _log_packet(&this->packet);
    }
  }
}

void trace_encoder_e::_encode_sync_packet() {
  this->buffer.clear();
  auto *a = std::get_if<sync_packet_t>(&this->packet);
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

    load_buffer(this->buffer, packet);
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

    load_buffer(this->buffer, packet);
    break;
  }
  default:
    break;
  }
}

void trace_encoder_e::_encode_branch_packet() {
  this->buffer.clear();
  auto *a = std::get_if<branch_map_packet_t>(&this->packet);
  switch (a->fmt) {
  case FMT_1: {
    // convert to bitstrings
    std::string packet = "";
    std::string fmt = std::bitset<2>(a->fmt).to_string();
    std::string branches = std::bitset<5>(a->branches).to_string();
    size_t branch_map_length;
    if (a->branches == 0) {
      branch_map_length = 31;
    } else if (a->branches <= 3) {
      branch_map_length = 3;
    } else if (a->branches <= 7) {
      branch_map_length = 7;
    } else if (a->branches <= 15) {
      branch_map_length = 15;
    } else if (a->branches < 31) {
      branch_map_length = 31;
    }
    std::string branch_map =
        std::bitset<31>(a->branch_map)
            .to_string()
            .substr(31 - branch_map_length, branch_map_length);
    std::string address =
        (a->branches != 0) ? std::bitset<63>(a->address).to_string() : ""; // TODO - more sound logic for addresses. there can be no-address packets when branches !- 0
    std::string notify = std::to_string(a->notify);
    std::string updiscon = std::to_string(a->updiscon);

    // reverse for easier processing
    std::reverse(fmt.begin(), fmt.end());
    std::reverse(branches.begin(), branches.end());
    std::reverse(branch_map.begin(), branch_map.end());
    std::reverse(address.begin(), address.end());

    packet.append(fmt);
    packet.append(branches);
    packet.append(branch_map);
    packet.append(address);
    packet.append(notify);
    packet.append(updiscon);

    load_buffer(this->buffer, packet);
    break;
  }
  case FMT_2: {
    std::string packet = "";

    std::string fmt = std::bitset<2>(a->fmt).to_string();
    std::string address = std::bitset<63>(a->address).to_string();
    std::string notify = std::to_string(a->notify);
    std::string updiscon = std::to_string(a->updiscon);

    std::reverse(fmt.begin(), fmt.end());
    std::reverse(address.begin(), address.end());

    packet.append(fmt);
    packet.append(address);
    packet.append(notify);
    packet.append(updiscon);

    load_buffer(this->buffer, packet);
    break;
  }
  default:
    break;
  }
}

uint32_t trace_encoder_e::_convert_branch_map() {
  uint32_t result = 0;
  size_t size = this->branches;
  for (size_t i = 0; i < size; ++i) {
    if (branch_map[size - 1 - i]) {
      result |= (1 << i);
    }
  }
  return result;
}

void trace_encoder_e::_log_packet(trace_encoder_e_packet_t *packet) {
  FILE *trace_log = this->trace_log;

  std::visit(
      [&](auto &&arg) {
        // You can now use local_var here
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, sync_packet_t>) {
          auto pkt = static_cast<sync_packet_t>(arg);
          fprintf(this->trace_log,
                  "[Packet]: fmt: %d, subfmt: %d, branch: %d, privilege: %d, "
                  "time: %d, ecause: %d, interrupt: %d, thaddr: %x, address: "
                  "%x, tval: %d\n",
                  pkt.fmt, pkt.subfmt, pkt.branch, pkt.privilege, pkt.time,
                  pkt.ecause, pkt.interrupt, pkt.thaddr, pkt.address << 1,
                  pkt.tval);
        } else if constexpr (std::is_same_v<T, branch_map_packet_t>) {
          auto pkt = static_cast<branch_map_packet_t>(arg);
          switch (pkt.fmt) {
          case FMT_1: {
            fprintf(this->trace_log,
                    "[Packet]: fmt: %d, branches: %d, branch_map: %d, address: "
                    "0x%x, notify: %d\n",
                    pkt.fmt, pkt.branches, pkt.branch_map, pkt.address << 1,
                    pkt.notify);
            break;
          }

          case FMT_2: {
            fprintf(this->trace_log,
                    "[Packet]: fmt: %d, address: %x, notify: %d\n", pkt.fmt,
                    pkt.address << 1, pkt.notify);
            break;
          }

          default:
            break;
          }
        }
      },
      *packet);
}

void trace_encoder_e::load_buffer(std::vector<uint8_t> buffer,
                                  std::string data) {
  this->num_bytes = 0;
  this->num_bits_uncompressed = data.size();
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
    for (int i = 0; i < 8 - remainder; i++) {
      padding += msb;
    }
    data = data + padding;
  }

  this->num_bytes = data.size() >> 3;

  // write to buffer
  for (int i = 0; i < num_bytes; i++) {
    std::string byte_string = data.substr(i * 8, 8);
    std::reverse(byte_string.begin(), byte_string.end());
    uint8_t byte = std::stoi(byte_string, nullptr, 2);
    this->buffer.push_back(byte);
  }
}