# Docker Monitoring Setup for GHOSTDAG Simulator

This directory contains a complete Docker Compose setup for monitoring your GHOSTDAG network simulator with Prometheus and Grafana.

## Quick Start

### 1. Start the monitoring stack

```bash
cd docker
docker-compose up -d
```

### 2. Access the services

- **Grafana**: http://localhost:3000
  - Username: `admin`
  - Password: `admin`

- **Prometheus**: http://localhost:9090

### 3. Run the simulator with Prometheus backend

```bash
# From the ghostdagsim root directory
/opt/ns-allinone-3.46.1/ns-3.46.1/ns3 run ghostdagsim -- \
  --nodes 10 \
  --miners 10 \
  --metricsBackend prometheus \
  --metricsPort 9090 \
  --metricsFlushInterval 5
```

Note: The simulator needs to expose port 9090 for Prometheus to scrape it. You may need to run the simulator with `--network=host` or adjust the prometheus target.

## Dashboard

The Grafana dashboard is automatically provisioned with:
- **Network Overview**: Total blocks, orphan rate, throughput, latency
- **Propagation Latency**: Avg, P50, P95, P99 over time
- **Orphan & Block Metrics**: Orphan rates, block counts, network traffic
- **Transaction Metrics**: Throughput, collisions, message counts
- **Node-level Metrics**: DAG size and mempool size per node

## Prometheus Targets

The simulator metrics are scraped every 2 seconds. The dashboard shows:
- `ghostdag_blocks_mined_total`
- `ghostdag_blocks_received_total`
- `ghostdag_blocks_orphaned_total`
- `ghostdag_propagation_latency_seconds`
- `ghostdag_propagation_p50_seconds`
- `ghostdag_propagation_p95_seconds`
- `ghostdag_propagation_p99_seconds`
- `ghostdag_orphan_rate`
- `ghostdag_orphan_resolved_rate`
- `ghostdag_throughput`
- `ghostdag_transaction_collision_rate`
- `ghostdag_bytes_sent_total`
- `ghostdag_bytes_received_total`
- `ghostdag_redundant_bytes_sent_total`
- `ghostdag_redundant_bytes_received_total`
- `ghostdag_messages_sent_total`
- `ghostdag_messages_received_total`
- `ghostdag_dag_size`
- `ghostdag_mempool_size`

## Configuration

### Prometheus
- Data retention: 7 days
- Scrape interval: 5s (2s for ghostdag job)
- Config: `prometheus/prometheus.yml`

### Grafana
- Default credentials: admin/admin
- Auto-provisioned dashboards and datasources
- Dark theme

## Stopping

```bash
cd docker
docker-compose down
```

To also remove volumes (clears all data):
```bash
docker-compose down -v
```
