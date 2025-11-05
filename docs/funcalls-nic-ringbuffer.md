# e1000e

## 分配

```c
struct net_device netdev.ethtool_ops = e1000_ethtool_ops = {
  .set_ringparam		= e1000_set_ringparam() {
    e1000_set_ringparam() {
      e1000e_setup_tx_resources() {
        e1000_alloc_ring_dma() {
          dma_alloc_coherent();
        }
      }

      e1000e_setup_rx_resources() {
        e1000_alloc_ring_dma() {
          dma_alloc_coherent();
        }
      }
    }
  },
};
```

## 接收

```c
e1000_clean_rx_irq() {
}
```

## 发送


# dma_alloc_coherent()

```c
// coherent: 相干
dma_alloc_coherent() {

}
```