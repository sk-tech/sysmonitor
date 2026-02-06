"""
Load testing for aggregator
Simulates multiple agents sending metrics concurrently
"""

import requests
import time
import random
import json
from multiprocessing import Process, Value, Array
from datetime import datetime
import sys


class LoadTestMetrics:
    """Shared metrics across processes"""
    def __init__(self):
        self.requests_sent = Value('i', 0)
        self.requests_failed = Value('i', 0)
        self.total_latency = Value('d', 0.0)
        self.max_latency = Value('d', 0.0)
        self.min_latency = Value('d', 999.0)


class AgentSimulator:
    """Simulates a single agent sending metrics"""
    
    def __init__(self, agent_id, aggregator_url, metrics_obj, duration=60):
        self.agent_id = agent_id
        self.aggregator_url = aggregator_url
        self.metrics = metrics_obj
        self.duration = duration
        self.hostname = f"load-test-agent-{agent_id}"
    
    def generate_metrics(self, count=10):
        """Generate fake metrics"""
        now = int(time.time())
        metrics = []
        
        for i in range(count):
            # CPU metrics
            metrics.append({
                "timestamp": now,
                "metric_type": "cpu.total_usage",
                "host": self.hostname,
                "value": random.uniform(10.0, 90.0),
                "tags": {"agent_id": str(self.agent_id)}
            })
            
            # Memory metrics
            metrics.append({
                "timestamp": now,
                "metric_type": "memory.usage_percent",
                "host": self.hostname,
                "value": random.uniform(30.0, 80.0),
                "tags": {"agent_id": str(self.agent_id)}
            })
            
            # Disk metrics
            metrics.append({
                "timestamp": now,
                "metric_type": "disk.usage_percent",
                "host": self.hostname,
                "value": random.uniform(40.0, 70.0),
                "tags": {"agent_id": str(self.agent_id)}
            })
        
        return metrics
    
    def run(self):
        """Run agent simulation"""
        start_time = time.time()
        
        print(f"Agent {self.agent_id} started")
        
        while time.time() - start_time < self.duration:
            try:
                # Generate and send metrics
                metrics = self.generate_metrics(count=10)
                
                request_start = time.time()
                response = requests.post(
                    f"{self.aggregator_url}/api/metrics",
                    json=metrics,
                    timeout=5.0
                )
                request_time = time.time() - request_start
                
                # Update shared metrics
                with self.metrics.requests_sent.get_lock():
                    self.metrics.requests_sent.value += 1
                
                with self.metrics.total_latency.get_lock():
                    self.metrics.total_latency.value += request_time
                
                with self.metrics.max_latency.get_lock():
                    if request_time > self.metrics.max_latency.value:
                        self.metrics.max_latency.value = request_time
                
                with self.metrics.min_latency.get_lock():
                    if request_time < self.metrics.min_latency.value:
                        self.metrics.min_latency.value = request_time
                
                if response.status_code != 200:
                    with self.metrics.requests_failed.get_lock():
                        self.metrics.requests_failed.value += 1
                
            except Exception as e:
                with self.metrics.requests_failed.get_lock():
                    self.metrics.requests_failed.value += 1
                print(f"Agent {self.agent_id} error: {e}")
            
            # Wait before next batch
            time.sleep(1.0)
        
        print(f"Agent {self.agent_id} completed")


def run_load_test(num_agents=100, duration=60, aggregator_url="http://localhost:9000"):
    """Run load test with multiple simulated agents"""
    
    print("=" * 70)
    print("SysMonitor Aggregator Load Test")
    print("=" * 70)
    print(f"Configuration:")
    print(f"  Agents: {num_agents}")
    print(f"  Duration: {duration} seconds")
    print(f"  Aggregator: {aggregator_url}")
    print(f"  Started: {datetime.now()}")
    print("=" * 70)
    
    # Check if aggregator is accessible
    try:
        response = requests.get(f"{aggregator_url}/health", timeout=5)
        if response.status_code != 200:
            print(f"ERROR: Aggregator health check failed (HTTP {response.status_code})")
            return
    except Exception as e:
        print(f"ERROR: Cannot connect to aggregator: {e}")
        print("Make sure the aggregator is running first.")
        return
    
    # Create shared metrics object
    metrics = LoadTestMetrics()
    
    # Start agent processes
    processes = []
    for i in range(num_agents):
        agent = AgentSimulator(i, aggregator_url, metrics, duration)
        p = Process(target=agent.run)
        p.start()
        processes.append(p)
        
        # Stagger starts slightly to avoid thundering herd
        if i % 10 == 0:
            time.sleep(0.1)
    
    print(f"\n{num_agents} agents started, running for {duration} seconds...")
    
    # Monitor progress
    start_time = time.time()
    try:
        while time.time() - start_time < duration:
            elapsed = int(time.time() - start_time)
            print(f"\rProgress: {elapsed}/{duration}s | "
                  f"Requests: {metrics.requests_sent.value} | "
                  f"Failed: {metrics.requests_failed.value}",
                  end='', flush=True)
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    
    # Wait for all processes to complete
    print("\n\nWaiting for agents to complete...")
    for p in processes:
        p.join(timeout=10)
        if p.is_alive():
            p.terminate()
    
    # Calculate statistics
    total_requests = metrics.requests_sent.value
    failed_requests = metrics.requests_failed.value
    success_requests = total_requests - failed_requests
    
    if total_requests > 0:
        avg_latency = metrics.total_latency.value / total_requests
        success_rate = (success_requests / total_requests) * 100
    else:
        avg_latency = 0
        success_rate = 0
    
    throughput = total_requests / duration
    
    # Print results
    print("\n")
    print("=" * 70)
    print("Load Test Results")
    print("=" * 70)
    print(f"Total requests:     {total_requests}")
    print(f"Successful:         {success_requests}")
    print(f"Failed:             {failed_requests}")
    print(f"Success rate:       {success_rate:.2f}%")
    print(f"Duration:           {duration}s")
    print(f"Throughput:         {throughput:.2f} req/s")
    print(f"Average latency:    {avg_latency*1000:.2f}ms")
    print(f"Min latency:        {metrics.min_latency.value*1000:.2f}ms")
    print(f"Max latency:        {metrics.max_latency.value*1000:.2f}ms")
    print("=" * 70)
    
    # Performance assessment
    print("\nPerformance Assessment:")
    if success_rate >= 99.0:
        print("✓ Excellent: >99% success rate")
    elif success_rate >= 95.0:
        print("✓ Good: >95% success rate")
    else:
        print("✗ Poor: <95% success rate")
    
    if avg_latency < 0.1:
        print("✓ Excellent: <100ms average latency")
    elif avg_latency < 0.5:
        print("✓ Good: <500ms average latency")
    else:
        print("✗ Poor: >500ms average latency")
    
    if throughput > 100:
        print(f"✓ Excellent: {throughput:.0f} req/s throughput")
    elif throughput > 50:
        print(f"✓ Good: {throughput:.0f} req/s throughput")
    else:
        print(f"✗ Poor: {throughput:.0f} req/s throughput")


def run_spike_test(aggregator_url="http://localhost:9000"):
    """Test handling of sudden traffic spikes"""
    
    print("=" * 70)
    print("Spike Test: Sudden Traffic Burst")
    print("=" * 70)
    
    metrics = LoadTestMetrics()
    
    # Phase 1: Normal load (10 agents)
    print("Phase 1: Normal load (10 agents) for 10s...")
    processes = []
    for i in range(10):
        agent = AgentSimulator(i, aggregator_url, metrics, duration=10)
        p = Process(target=agent.run)
        p.start()
        processes.append(p)
    
    time.sleep(10)
    for p in processes:
        p.join()
    
    phase1_requests = metrics.requests_sent.value
    print(f"Phase 1 complete: {phase1_requests} requests")
    
    # Phase 2: Spike (100 agents)
    print("\nPhase 2: SPIKE (100 agents) for 20s...")
    processes = []
    for i in range(100):
        agent = AgentSimulator(i + 10, aggregator_url, metrics, duration=20)
        p = Process(target=agent.run)
        p.start()
        processes.append(p)
    
    time.sleep(20)
    for p in processes:
        p.join()
    
    phase2_requests = metrics.requests_sent.value - phase1_requests
    print(f"Phase 2 complete: {phase2_requests} requests")
    
    # Phase 3: Back to normal (10 agents)
    print("\nPhase 3: Back to normal (10 agents) for 10s...")
    processes = []
    for i in range(10):
        agent = AgentSimulator(i + 110, aggregator_url, metrics, duration=10)
        p = Process(target=agent.run)
        p.start()
        processes.append(p)
    
    time.sleep(10)
    for p in processes:
        p.join()
    
    phase3_requests = metrics.requests_sent.value - phase1_requests - phase2_requests
    print(f"Phase 3 complete: {phase3_requests} requests")
    
    # Results
    total_requests = metrics.requests_sent.value
    failed_requests = metrics.requests_failed.value
    success_rate = ((total_requests - failed_requests) / total_requests * 100) if total_requests > 0 else 0
    
    print("\n" + "=" * 70)
    print("Spike Test Results:")
    print(f"Total requests:     {total_requests}")
    print(f"Failed:             {failed_requests}")
    print(f"Success rate:       {success_rate:.2f}%")
    print("=" * 70)
    
    if success_rate >= 95.0:
        print("✓ System handled spike well")
    else:
        print("✗ System degraded under spike")


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Load test for SysMonitor aggregator")
    parser.add_argument("--agents", type=int, default=100, help="Number of simulated agents")
    parser.add_argument("--duration", type=int, default=60, help="Test duration in seconds")
    parser.add_argument("--url", default="http://localhost:9000", help="Aggregator URL")
    parser.add_argument("--spike", action="store_true", help="Run spike test instead")
    
    args = parser.parse_args()
    
    if args.spike:
        run_spike_test(args.url)
    else:
        run_load_test(args.agents, args.duration, args.url)
