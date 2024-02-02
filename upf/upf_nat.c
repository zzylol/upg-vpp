/*
 * Copyright (c) 2019 Travelping GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <vppinfra/error.h>
#include <vppinfra/hash.h>
#include <vnet/vnet.h>
#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/dpo/drop_dpo.h>
#include <vnet/dpo/load_balance.h>
#include <vnet/interface_output.h>

#include <upf/upf.h>
#include <upf/upf_pfcp.h>

#if CLIB_DEBUG > 2
#define upf_debug clib_warning
#else
#define upf_debug(...)                                                        \
  do                                                                          \
    {                                                                         \
    }                                                                         \
  while (0)
#endif

#ifndef CLIB_MARCH_VARIANT



const static char *const upf_session_dpo_ip4_nodes[] = {
  "upf-ip4-session-dpo",
  NULL,
};

const static char *const upf_session_dpo_ip6_nodes[] = {
  "upf-ip6-session-dpo",
  NULL,
};

const static char *const *const upf_session_dpo_nodes[DPO_PROTO_NUM] = {
  [DPO_PROTO_IP4] = upf_session_dpo_ip4_nodes,
  [DPO_PROTO_IP6] = upf_session_dpo_ip6_nodes,
};

#endif /* CLIB_MARCH_VARIANT */

/* Statistics (not all errors) */
#define foreach_upf_session_dpo_error                                         \
  _ (SESSION_DPO, "good packets session_dpo")                                 \
  _ (PROXY_LOOP, "proxy output loop detected")

static char *upf_session_dpo_error_strings[] = {
#define _(sym, string) string,
  foreach_upf_session_dpo_error
#undef _
};

typedef enum
{
#define _(sym, str) UPF_NAT_ERROR_##sym,
  foreach_upf_session_dpo_error
#undef _
    UPF_SESSION_DPO_N_ERROR,
} upf_session_dpo_error_t;

typedef enum
{
  UPF_NAT_NEXT_DROP,
  UPF_NAT_NEXT_ICMP_ERROR,
  UPF_NAT_NEXT_FLOW_PROCESS,
  UPF_NAT_N_NEXT,
} upf_session_dpo_next_t;

typedef struct
{
  u32 session_index;
  u64 up_seid;
  u8 packet_data[64 - 1 * sizeof (u32)];
} upf_session_dpo_trace_t;

static u8 *
format_upf_session_dpo_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  upf_session_dpo_trace_t *t = va_arg (*args, upf_session_dpo_trace_t *);
  u32 indent = format_get_indent (s);

  s = format (s, "upf_session%d seid %d \n%U%U", t->session_index, t->up_seid,
              format_white_space, indent, format_ip4_header, t->packet_data,
              sizeof (t->packet_data));
  return s;
}

VLIB_NODE_FN (upf_nat_node)
(vlib_main_t *vm, vlib_node_runtime_t *node, vlib_frame_t *from_frame)
{
  vlib_node_runtime_t *error_node =
    vlib_node_get_runtime (vm, ip4_input_node.index);
  u32 n_left_from, next_index, *from, *to_next;
  upf_main_t *gtm = &upf_main;

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  u16 next = 0;
  u32 sidx = 0;

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;
      vlib_buffer_t *b;
      u32 bi;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      /* TODO: dual and maybe quad loop */
      while (n_left_from > 0 && n_left_to_next > 0)
        {
          ip4_header_t *ip0;
          u32 error0;

          bi = from[0];
          to_next[0] = bi;
          from += 1;
          to_next += 1;
          n_left_from -= 1;
          n_left_to_next -= 1;

          b = vlib_get_buffer (vm, bi);

          sidx = vnet_buffer (b)->ip.adj_index[VLIB_TX];
          upf_debug ("Session %d (0x%08x)", sidx, sidx);

          // UPF_ENTER_SUBGRAPH (b, sidx, 1);
          error0 = IP4_ERROR_NONE;
          next = UPF_NAT_NEXT_FLOW_PROCESS;
          upf_debug ("IP hdr: %U", format_ip4_header, ip0, b->current_length);



          ip4_header_t *ip4 = vlib_buffer_get_current (b);
          tcp_header_t *tcp = ip4_next_header (ip4);
          // ip4->src_address.as_u32 = 0xAAAAAAAA;
          // upf_debug ("QQQQQQQ IP hdr: %U", format_ip4_header,
          //            vlib_buffer_get_current (b), b->current_length);
          //
          int l3_csum_delta = 0;
          int l4_csum_delta = 0;
          if (ip4->dst_address.as_u32 == 0x14000090 &&
              ip4->src_address.as_u32 == 0x301000a)
            {
              ip4->dst_address.as_u32 = 0x300010a;
              l3_csum_delta = ip_csum_add_even (l3_csum_delta, 0x300010a);
              l3_csum_delta = ip_csum_sub_even (l3_csum_delta, 0x14000090);
              // l3_csum_delta = ip_csum_add_even (l3_csum_delta,
              // 0x300010a); l3_csum_delta = ip_csum_sub_even
              // (l3_csum_delta, 0x14000090);
            }

          // // int delta = ip_csum_add_even (0, ip4->src_address.as_u32);
          // // ip_sum = ip_csum_add_even (ip_sum, delta);
          // // ip4->checksum = ip_csum_fold (ip_sum);
          //
          // ip_csum_t tcp_sum = tcp->checksum;
          // tcp_sum = ip_csum_sub_even (tcp_sum, f->l3_csum_delta);
          // tcp_sum = ip_csum_sub_even (tcp_sum, f->l4_csum_delta);
          // mss_clamping (sm->mss_clamping, tcp, &tcp_sum);
          // tcp->checksum = ip_csum_fold (tcp_sum);
          //

          ip_csum_t tcp_sum = tcp->checksum;
          tcp_sum = ip_csum_sub_even (tcp_sum, l3_csum_delta);
          tcp_sum = ip_csum_sub_even (tcp_sum, l4_csum_delta);
          // mss_clamping (sm->mss_clamping, tcp, &tcp_sum);
          tcp->checksum = ip_csum_fold (tcp_sum);

          ip_csum_t ip_sum = ip4->checksum;
          ip_sum = ip_csum_sub_even (ip_sum, l3_csum_delta);
          ip4->checksum = ip_csum_fold (ip_sum);

          // ip4->checksum = 0;
          // ip4->checksum = ip4_header_checksum (ip4);

          clib_warning ("QQQQQZaaaaanat ip4->src_address.as_u32 = %x, "
                        "ip4->dst_address.as_u32 = %x",
                        ip4->src_address.as_u32, ip4->dst_address.as_u32);



          // ip4_ttl_and_checksum_check (b, ip0, &next, &error0);
          // vnet_calc_checksums_inline (vm, b, 1 /* is_ip4 */, 0 /* is_ip6 */);

        trace:
          b->error = error_node->errors[error0];
          if (PREDICT_FALSE (b->flags & VLIB_BUFFER_IS_TRACED))
            {
              upf_session_t *sess = pool_elt_at_index (gtm->sessions, sidx);
              upf_session_dpo_trace_t *tr =
                vlib_add_trace (vm, node, b, sizeof (*tr));
              tr->session_index = sidx;
              tr->up_seid = sess->up_seid;
              clib_memcpy (tr->packet_data, vlib_buffer_get_current (b),
                           sizeof (tr->packet_data));
            }

          vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,
                                           n_left_to_next, bi, next);
        }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

/* clang-format off */
VLIB_REGISTER_NODE (upf_nat_node) = {
  .name = "upf-nat-test",
  .vector_size = sizeof (u32),
  .format_trace = format_upf_session_dpo_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,

  .n_errors = ARRAY_LEN(upf_session_dpo_error_strings),
  .error_strings = upf_session_dpo_error_strings,

  .n_next_nodes = UPF_NAT_N_NEXT,
  .next_nodes = {
    [UPF_NAT_NEXT_DROP]         = "error-drop",
    [UPF_NAT_NEXT_ICMP_ERROR]   = "ip4-icmp-error",
    [UPF_NAT_NEXT_FLOW_PROCESS] = "upf-ip4-flow-process",
  },
};
