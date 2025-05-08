
# send(2)

```c
send(fd, buff, ..., addr, ...) {
  __sys_sendto(fd, buff, ..., addr) {
    struct socket *sock = sockfd_lookup_light(fd, ...);
    struct msghdr msg = /* buff */;
    sock_sendmsg(sock, &msg) {
      sock_sendmsg_nosec(sock, msg) {
        /* tcp ipv4, inet6_sendmsg if ipv6 */
        inet_sendmsg(sock, msg) {
          /* tcp, udp_sendmsg if udp */
          tcp_sendmsg(sock, msg) {
            lock_sock(sock);
            tcp_sendmsg_locked(sock, msg) {
              struct sockcm_cookie sockc;
              #if 0 /* 不考虑零拷贝 */
              if (MSG_ZEROCOPY) {
                struct sk_buff *skb = tcp_write_queue_tail(sock);
                struct ubuf_info *uarg = sock_zerocopy_realloc(sk, size, skb_zcopy(skb));
              }
              #endif
              sockcm_init(&sockc, sock);
              while (msg_data_left(msg)) {
                skb = tcp_write_queue_tail(sk);
                /* ... */
                skb_copy_to_page_nocache(sock, msg, skb);
                /* ... */
                tcp_push_one(sock, ...) {
                  tcp_write_xmit(sock, ...) {
                    while ((skb = tcp_send_head(sock))) {
                      /* ...处理小包（nagle算法），TSO等... */
                      tcp_transmit_skb(sock, skb, clone_it=1, ...) {
                        __tcp_transmit_skb(sock, skb, clone_it=1, ...) {
                          struct tcphdr *th = (struct tcphdr *)skb->data;
                          /* build TCP header, set Urge here */
                          /* send to IP, inet6_csk_xmit if ipv6 */
                          ip_queue_xmit(sock, skb) {
                            __ip_queue_xmit(sock, skb) {
                              struct inet_sock *inet = inet_sk(sock);
                              struct net *net = sock_net(sock);
                              rcu_read_lock();
                              struct iphdr *iph = ip_hdr(skb);
                              /* build IP header... */
                              ip_local_out(net, sock, skb) {
                                __ip_local_out(net, sock, skb) {
                                  /* checksum, netfilter, ... */
                                  dst_output(net, sock, skb) {
                                    /* skb_dst(skb)->output(net, sock, skb) */
                                    ip_output(net, sock, skb) {
                                      struct net_device *dev = skb_dst(skb)->dev;
                                      struct net_device *indev = skb->dev;
                                      skb->dev = dev;
                                      ip_finish_output(net, sock, skb) {
                                        __ip_finish_output(net, sock, skb) {
                                          if (/* Generic TCP Segmentation Offload */) {
                                            ip_finish_output_gso(net, sock, skb) {
                                              /* TODO */
                                            }
                                          } else {
                                            ip_finish_output2(net, sock, skb) {
                                              rcu_read_lock_bh();
                                              /* find route neighbour */
                                              struct neighbour *neigh = ip_neigh_for_gw(..., skb, ...) {
                                                if (AF_INET) {
                                                  return ip_neigh_gw4(dev, ...) {
                                                    /* TODO */
                                                  }
                                                }
                                              }
                                              sock_confirm_neigh(skb, neigh);
                                              neigh_output(neigh, skb) {

                                              }
                                              rcu_read_unlock_bh();
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                              rcu_read_unlock();
                            }
                          }
                        }
                      }
                      tcp_event_new_data_sent(sock, skb) {
                        struct tcp_sock *tp = tcp_sk(sock);
                        tcp_rbtree_insert(&sock->tcp_rtx_queue, skb);
                        tp->packets_out += tcp_skb_pcount(skb); /* 计数 */
                      }
                    }
                  }
                }
              }
            }
            release_sock(sock);
          }
        }
      }
    }
  }
}
```